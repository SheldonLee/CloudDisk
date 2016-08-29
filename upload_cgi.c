#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "fcgi_config.h"
#include "fcgi_stdio.h"
#include "util_cgi.h"
#include "make_log.h"
#include "redis_op.h"


int GetFileContent(int len, char *file_buf, char *fdfs_file_name);

int UploadFile(char *fdfs_file_name, char *fdfs_file_path);

int GetFileUrl(char *fdfs_file_path, char *fdfs_file_url);

int SaveFileInfotoRedis(char *filename, char *fdfs_file_path, char *fdfs_file_url);
int main ()
{
    char *file_buf = NULL;
    char fdfs_file_name[128] = {0};   //存放上传文件名
    char fdfs_file_path[256] = {0};   //存放上传文件在
    char fdfs_file_host_name[128] = {0};
    char fdfs_file_stat_buf[256] = {0};    
    char fdfs_file_url[512] = {0};

    while (FCGI_Accept() >= 0) 
    {
        char *contentLength = getenv("CONTENT_LENGTH");
        int len;

        //加上http头，让对端浏览器得以识别
		printf("Content-type: text/html\r\n");
        printf("\r\n");

        if (contentLength != NULL) 
        {
            len = strtol(contentLength, NULL, 10);
        }
        else 
        {
            len = 0;
        }

        if (len <= 0)
        {
            printf("No data from standard input\n");
        }
        else 
        {
        	//解析http数据包，获取上传文件内容
            GetFileContent(len, file_buf, fdfs_file_name);
            
            //将该文件存入fastDFS中,并得到文件的file_id
			UploadFile(fdfs_file_name, fdfs_file_path);   //上传文件
			
			 //得到文件所存放storage的host_name
			GetFileUrl(fdfs_file_path, fdfs_file_url);
			
			
			SaveFileInfotoRedis(fdfs_file_name, fdfs_file_path, fdfs_file_url);
			unlink(fdfs_file_name);
			memset(fdfs_file_name, 0, 128);
            memset(fdfs_file_path, 0, 256);
            memset(fdfs_file_stat_buf, 0, 256);
            memset(fdfs_file_host_name, 0, 128);
            memset(fdfs_file_url, 0, 512);
			if(file_buf != 0)
				free(file_buf);
            //printf("date: %s\r\n", getenv("QUERY_STRING"));
        }
    } /* while */

    return 0;
}

int GetFileContent(int len, char *file_buf, char *fdfs_file_name)
{
	
	int i, ch;
    char *begin = NULL;
    char *end = NULL;
    char *p, *q, *k;
    char boundary[256] = {0};
    char content_text[256] = {0};
    char filename[128] = {0};
	
    //==========> 开辟存放文件的 内存 <===========
	LOG("distributed_memory", "upload_cgi", "开始解析http数据包");
    file_buf = malloc(len);
    if (file_buf == NULL) {
        printf("malloc error! file size is to big!!!!\n");
        return -1;
    }

    begin = file_buf;
    p = begin;
    for (i = 0; i < len; i++) {
        if ((ch = getchar()) < 0) {
            printf("Error: Not enough bytes received on standard input<p>\n");
            break;
        }
        //putchar(ch);
        *p = ch;
        p++;
    }

    //===========> 开始处理前端发送过来的post数据格式 <============
    //begin deal
    end = p;

    p = begin;

    //get boundary
    p = strstr(begin, "\r\n");
    if (p == NULL) {
        printf("wrong no boundary!\n");
        goto END;
    }

    strncpy(boundary, begin, p-begin);
    boundary[p-begin] = '\0';
    //printf("boundary: [%s]\n", boundary);

    p+=2;//\r\n
    //已经处理了p-begin的长度
    len -= (p-begin);

    //get content text head
    begin = p;

    p = strstr(begin, "\r\n");
    if(p == NULL) {
        printf("ERROR: get context text error, no filename?\n");
        goto END;
    }
    strncpy(content_text, begin, p-begin);
    content_text[p-begin] = '\0';
    //printf("content_text: [%s]\n", content_text);

    p+=2;//\r\n
    len -= (p-begin);

    //get filename
    // filename="123123.png"
    //           ↑
    q = begin;
    q = strstr(begin, "filename=");
    
    q+=strlen("filename=");
    q++;

    k = strchr(q, '"');
    strncpy(filename, q, k-q);
    filename[k-q] = '\0';

    trim_space(filename);
    //printf("filename: [%s]\n", filename);

    //get file
    begin = p;     
    p = strstr(begin, "\r\n");
    p+=4;//\r\n\r\n
    len -= (p-begin);

    begin = p;
    // now begin -->file's begin
    //find file's end
    p = memstr(begin, len, boundary);
    if (p == NULL) {
        p = end-2;    //\r\n
    }
    else {
        p = p -2;//\r\n
    }

    //begin---> file_len = (p-begin)

    //=====> 此时begin-->p两个指针的区间就是post的文件二进制数据
    //======>将数据写入文件中,其中文件名也是从post数据解析得来  <===========
  
    int fd = 0;
    fd = open(filename, O_CREAT|O_WRONLY, 0644);
    if (fd < 0) {
        LOG("distributed_memory", "upload_cgi","open %s error\n", filename);
        goto END;
    }

    ftruncate(fd, (p-begin));
    write(fd, begin, (p-begin));
    close(fd); 
    strcpy(fdfs_file_name, filename);
    LOG("distributed_memory", "upload_cgi", "完成http数据包解析，将文件写入本地");
END:
	memset(boundary, 0, 256);
    memset(content_text, 0, 256);
    memset(filename, 0, 256);
	return 0;
	
}

int UploadFile(char *fdfs_file_name, char *fdfs_file_path)
{
	LOG("distributed_memory", "upload_cgi", "开始执行文件上传"); 
	

	//printf("fdfs_file_name = %s\n", fdfs_file_name);
	int ret, pipfd[2];   //管道描述符
    int forkfd;          //创建进程描述符
	char rawurl[256]={0};
	
    ret = pipe(pipfd);
    if(ret < 0)
    {
       LOG("distributed_memory", "upload_cgi", "UploadFile 管道创建失败");
       return ret;
    }
    forkfd = fork();
    if(forkfd < 0)
    {
        LOG("distributed_memory", "upload_cgi", "UploadFile 创建子进程失败");
        return forkfd;
    }
    else if(forkfd == 0)
    {
        //子进程
        close(pipfd[0]);                    //子进程关闭管道读端
        dup2(pipfd[1], STDOUT_FILENO);      //重定向子进程输出文件描述符到管道写端
        dup2(pipfd[1], STDERR_FILENO);
        //执行fastDFS的客户端上传模块
        LOG("distributed_memory", "upload_cgi", "UploadFile fdfs_file_name : %s", fdfs_file_name); 
        execlp("fdfs_upload_file", "fdfs_upload_file" , "/etc/fdfs/client.conf", fdfs_file_name , NULL);      
    }

    //父进程负责接收子进程的返回URL
    
    close(pipfd[1]);    //父进程关闭管道写端
    //dup2(pipfd[0], STDIN_FILENO);    //不需要再将父进程的0号文件描述符重定向
    read(pipfd[0],rawurl, sizeof(rawurl));    
    LOG("distributed_memory", "upload_cgi", "rawurl = %s",rawurl); 
    wait(NULL);     //回收子进程              
    rawurl[strlen(rawurl)-1]='\0';		//这里有个坑，获取到的storage中的存储路径中有\r\n，需要把这个去掉
    strcpy(fdfs_file_path, rawurl);     
    close(pipfd[0]);    //使用完毕后将该描述符关掉，减少资源浪费
    LOG("distributed_memory", "upload_cgi", "fdfs_file_path : %s", fdfs_file_path);
    LOG("distributed_memory", "upload_cgi", "文件上传完成");
    return 0;
}

int GetFileUrl(char *fdfs_file_path, char *fdfs_file_url)
{
	int ret =0;
	int forkfd;
    int pipfd[2];
    
    LOG("distributed_memory", "upload_cgi", "开始获取上传文件路径");
    ret = pipe(pipfd);
    if(ret < 0)
    {
        ret = -1;
        LOG("distributed_memory", "upload_cgi", "解析URL时，创建管道失败");
        return ret;
    }
    forkfd = fork();
    if(forkfd < 0)
    {
        ret = -1;
        LOG("distributed_memory", "upload_cgi", "解析URL时，创建进程失败");
        return ret;
    }
    else if(forkfd == 0)
    {
        close(pipfd[0]);//子进程关闭读端
        dup2(pipfd[1], STDOUT_FILENO);
        dup2(pipfd[1], STDERR_FILENO);
        
        execlp("fdfs_file_info","fdfs_file_info", "/etc/fdfs/client.conf", fdfs_file_path, NULL);
        LOG("distributed_memory", "upload_cgi", "fdfs_file_info error!");
        ret = -1;
        return ret;
    }
    else if(forkfd > 0)
    {
        close(pipfd[1]);
        //dup2(pipfd[0], STDIN_FILENO);
        
        char fileinfo[512] = {0};
        char *filepoint1 = NULL, *filepoint2 = NULL;
        read(pipfd[0], fileinfo, sizeof(fileinfo));   //文件状态
        
        
        
        
        filepoint1 = strstr(fileinfo, "address:") + 9; //到IP开始部分
        filepoint2 = strstr(fileinfo, "file create timestamp") - 1; //到IP结尾部分
        strcat(fdfs_file_url,"http://");
        strncat(fdfs_file_url, filepoint1,filepoint2 - filepoint1);
        strcat(fdfs_file_url,":8888/");
        strcat(fdfs_file_url, fdfs_file_path); 
               
        
        close(pipfd[0]);  //使用完毕后将该描述符关掉，减少资源浪费               
        wait(NULL);
    }
    LOG("distributed_memory", "upload_cgi", "上传文件路径获取成功");
    printf("url = %s\n", fdfs_file_url);				//前台打印
    LOG("distributed_memory", "upload_cgi", "UploadFile fdfs_file_url = %s", fdfs_file_url);
	return ret;	
	
}


int SaveFileInfotoRedis(char *filename, char *fdfs_file_path, char *fdfs_file_url)
{
	char *redis_value_buf = NULL;
	redisContext *redis_conn = NULL;
	redis_conn = rop_connectdb_nopwd("203.189.235.47", "6379");
	if(redis_conn == NULL)
	{
		LOG("distributed_memory", "upload_cgi", "连接数据库失败");
		return -1;
	}
	
	redis_value_buf = (char *)malloc(VALUES_ID_SIZE);
	if(redis_value_buf == NULL)
	{
		LOG("distributed_memory", "upload_cgi", "分配redis value 错误");
		goto SaveFileInfotoRedis_END;
	}
	memset(redis_value_buf, 0 , VALUES_ID_SIZE);
	
	//file_id
	strcat(redis_value_buf, fdfs_file_path);
	strcat(redis_value_buf, REDIS_DILIMT);
	
	//url
	strcat(redis_value_buf, fdfs_file_url);
	strcat(redis_value_buf, REDIS_DILIMT);
	
	//filename
	strcat(redis_value_buf, filename);
	strcat(redis_value_buf, REDIS_DILIMT);
	
	//create time
	time_t now = time(NULL);
	char create_time[25] = {0};
	strftime(create_time, 24, "%Y-%m-%d %H:%M:%S", localtime(&now));	
	strcat(redis_value_buf, create_time);
	strcat(redis_value_buf, REDIS_DILIMT);
	//user
	strcat(redis_value_buf, "sheldon lee");
	strcat(redis_value_buf, REDIS_DILIMT);
	
	//type
	char suffix[10] = {0};
	get_file_suffix(filename, suffix);
	strcat(redis_value_buf, suffix);
	
	rop_list_push(redis_conn, FILE_INFO_LIST, redis_value_buf);		//文件信息表
	
	rop_zset_increment(redis_conn, FILE_HOT_ZSET, fdfs_file_path);   //文件点击量表
	
	LOG("distributed_memory", "upload_cgi", "存入数据: %s|%s|%s|%s|%s|%s|",fdfs_file_path,fdfs_file_url,filename,create_time,"sheldon lee",suffix);
SaveFileInfotoRedis_END:
	
	rop_disconnect(redis_conn);
	if(redis_value_buf != NULL)
		free(redis_value_buf);
	
	return 0;
}

