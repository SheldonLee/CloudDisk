CC=gcc
CPPFLAGS=-I./include -I/usr/local/include 
CFLAGS=-Wall 
LIBS=-lfcgi -lhiredis -lm

#找到当前目录下所有的.c文件
src = $(wildcard *.c)

#将当前目录下所有的.c  转换成.o给obj
obj = $(patsubst %.c, %.o, $(src))

upload=cgi_bin/upload
data=cgi_bin/data


target=$(upload) $(data) 


ALL:$(target)


#生成所有的.o文件
$(obj):%.o:%.c
	$(CC) -c $< $(CPPFLAGS) $(CFLAGS) -o $@ 


#upload cgi程序
$(upload):upload_cgi.o util_cgi.o make_log.o redis_op.o 
	$(CC) $^  $(LIBS) -o $@ 


#data cgi程序
$(data): data_cgi.o util_cgi.o make_log.o redis_op.o cJSON.o  
	$(CC) $^  $(LIBS)  -o $@ 


#clean指令

clean:
	-rm -rf $(obj) $(target)

distclean:
	-rm -rf $(obj) $(target)

#将clean目标 改成一个虚拟符号
.PHONY: clean ALL distclean
