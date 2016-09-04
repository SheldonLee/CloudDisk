#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>

#include "fcgi_stdio.h"      //fcgi标准io文件
#include "fcgi_config.h"      //fcgi配置文件
#include "redis_op.h"         //redis数据库操作
#include "util_cgi.h"		  //自定义工具库
#include "cJSON.h"            //json C API
#include "make_log.h"

extern char g_host_name[HOST_NAME_LEN];

void increase_file_pv(char *fileId);
void print_file_list_json(int fromId, int count, char *cmd, char *kind);
int main()
{
	
	char fromId[5] = {0};
	char count[5] = {0};
	char cmd[20] = {0};
	char kind[10] = {0};
	char fileId[FILE_NAME_LEN] = {0};
	
	while (FCGI_Accept() >= 0) 
	{
		char *query = getenv("QUERY_STRING");
		query_parse_key_value(query, "cmd", cmd, NULL);
		
		if(strcmp(cmd, "newFile") == 0)
		{
			//http://192.168.42.47/data?cmd=newFile&fromId=0&count=8
			//加载网页命令，从文件列表中第0个开始，发送8个
			query_parse_key_value(query, "fromId", fromId, NULL);
			query_parse_key_value(query, "count", count, NULL);
			query_parse_key_value(query, "kind", kind, NULL);
			
			LOG("distributed_memory", "data_cgi", "请求数据包：%s", query);
			cgi_init();
			
			//加上http头，让对端浏览器得以识别
			printf("Content-type: text/html\r\n");
            printf("\r\n");
						
			print_file_list_json(atoi(fromId), atoi(count), cmd, kind);
		}
		else if(strcmp(cmd, "increase") == 0)
		{
			//http://192.168.42.47/data?cmd=increase&fileId=group1%2FM00%2F00%2F00%2FOkC1eVfDci2AP22WAAB5cdzrrKY098.jpg
			query_parse_key_value(query, "fileId", fileId, NULL);
			
			LOG("distributed_memory", "data_cgi", "请求数据包：%s", query);
			
			str_replace(fileId, "%2F", "/");     //    %2F == /
			
			increase_file_pv(fileId);
			
			printf("Content-type: text/html\r\n");
            printf("\r\n");
		}
	
	}
	return 0;
}
void print_file_list_json(int fromId, int count, char *cmd, char *kind)
{
	int i = 0;
	
	
	cJSON *root = NULL;
	cJSON *array = NULL;
	char *out;
	char filename[FILE_NAME_LEN] = {0};			//文件名
	char create_time[FIELD_ID_SIZE] = {0};		//创建时间
	char picurl[PIC_URL_LEN] = {0};     		//静态图片路径
	char suffix[8] = {0};						//文件类型: .jpg .txt .png
	char pic_name[PIC_NAME_LEN] = {0};			//静态图片名
	char file_url[FILE_NAME_LEN] = {0};         //文件URL
	char file_id[FILE_NAME_LEN] = {0};			//group/00/00/*****
	
	int retn = 0;								//函数调用返回值
	int score = 0;								//点击量 pv
	
	RVALUES file_list_values = NULL;			//typedef char (*RVALUES)[VALUES_ID_SIZE];
	int value_num;
	redisContext *redis_conn = NULL;			//redis数据库连接句柄
	
	
	redis_conn = rop_connectdb_nopwd("203.189.235.47", "6379");
	if(redis_conn == NULL)
	{
		LOG("distributed_memory", "data_cgi", "redis connected error");
		return ;
	}
	
	LOG("distributed_memory", "data_cgi", "fromId:%d, count:%d", fromId, count);
	
	file_list_values = malloc(count * VALUES_ID_SIZE);
	
	//获取数据库list表中的[fromId, fromId+count-1]条数据
	retn = rop_range_list(redis_conn, FILE_INFO_LIST, fromId, fromId + count -1, file_list_values, &value_num);
	if(retn < 0)
	{
		LOG("distributed_memory", "data_cgi", "redis range list error");
		rop_disconnect(redis_conn);
		return;
	}
	
	LOG("distributed_memory", "data_cgi", "redis range list value_num = %d", value_num);
	
	root = cJSON_CreateObject();
	array = cJSON_CreateArray();
	for(i = 0;i < value_num; i++)
	{
		cJSON* item = cJSON_CreateObject();
		LOG("distributed_memory", "data_cgi", "----------------------%d-begin--------------------\n",i);
		/*
			{
				"id": "group1/M00/00/00/wKgCbFepT0SAOBCtACjizOQy1fU405.rar",
				"kind": 2,
				"title_m": "阶段测试_STL_数据结构.rar",
				"title_s": "文件title_s",
				"descrip": "2016-08-09 11:34:23",
				"picurl_m": "http://172.16.0.148/static/file_png/rar.png",
				"url": "http://192.168.2.108/group1/M00/00/00/wKgCbFepT0SAOBC
				tACjizOQy1fU405.rar",
				"pv": 1,
				"hot": 0
			},
		*/	
		
		//id
		get_value_by_col(file_list_values[i], 1, file_id, VALUES_ID_SIZE - 1, 0);
		cJSON_AddStringToObject(item, "id", file_id);
		LOG("distributed_memory", "data_cgi", "id = %s\n", file_id);		
		
		//kind
		cJSON_AddNumberToObject(item, "kind", 2);   
		
		//title_m(filename)
		get_value_by_col(file_list_values[i], 3, filename, VALUES_ID_SIZE - 1, 0);
		cJSON_AddStringToObject(item, "title_m", filename);
		LOG("distributed_memory", "data_cgi", "filename = %s\n", filename);		
		
		//title_s
        cJSON_AddStringToObject(item, "title_s", "文件title_s");
        
        //descrip(time)
        get_value_by_col(file_list_values[i], 4, create_time, VALUES_ID_SIZE - 1, 0);
		cJSON_AddStringToObject(item, "descrip", create_time);
		LOG("distributed_memory", "data_cgi", "create_time = %s\n", create_time);		
		
		//picurl_m		
		strcat(picurl, g_host_name);
		//strcat(picurl, ":8888");
		strcat(picurl, "/static/file_png/");
		
		get_file_suffix(filename, suffix);
		sprintf(pic_name, "%s.png", suffix);
		strcat(picurl, pic_name);
		cJSON_AddStringToObject(item, "picurl_m", picurl);
		LOG("distributed_memory", "data_cgi", "picurl = %s\n", picurl);		
		
		//url
		get_value_by_col(file_list_values[i], 2, file_url, VALUES_ID_SIZE - 1, 0);
		cJSON_AddStringToObject(item, "url", file_url);
		LOG("distributed_memory", "data_cgi", "file_url = %s\n", file_url);		
		
		//pv
		score = rop_zset_get_score(redis_conn, FILE_HOT_ZSET, file_id);
		cJSON_AddNumberToObject(item, "pv", score - 1);
		
		//hot
		 cJSON_AddNumberToObject(item, "hot", 0);		
		
		cJSON_AddItemToArray(array, item);
			
		
		memset(file_id, 0, FILE_NAME_LEN);
		memset(filename, 0, FILE_NAME_LEN);
		memset(file_url, 0, FILE_NAME_LEN);
		memset(create_time, 0, FIELD_ID_SIZE);
		memset(suffix, 0, 8);
		memset(picurl, 0, PIC_URL_LEN);
		memset(pic_name, 0, PIC_NAME_LEN);
		
		
		LOG("distributed_memory", "data_cgi", "----------------------%d-end--------------------\n",i);
	}
	
	cJSON_AddItemToObject(root, "games", array);
	out = cJSON_Print(root);
	LOG("distributed_memory", "data_cgi", "%s", out);
	printf("%s\n", out);    //发送到浏览器
	
	free(file_list_values);
    free(out);
    
    rop_disconnect(redis_conn);
}


void increase_file_pv(char *fileId)
{
	
	redisContext *redis_conn = NULL;			//redis数据库连接句柄
	
	redis_conn = rop_connectdb_nopwd("203.189.235.47", "6379");
	if(redis_conn == NULL)
	{
		LOG("distributed_memory", "data_cgi", "increase_file_pv redis connected error");
		return ;
	}
	
	//增加文件点击量
	rop_zset_increment(redis_conn, FILE_HOT_ZSET, fileId);
	
	rop_disconnect(redis_conn);
	
}
