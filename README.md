## 分布式网络云盘
> 作者：李宝洲     <br />
> 邮箱：li125008@163.com

### 1.项目简介
本项目是参考[七牛极简图床](http://yotuku.cn/)利用Nginx,fastDFS,redi,MySql搭建的一个分布式文件存储系统，支持用户上传图片，音乐，文档等资料。项目总体架构图如下：<br />
Nginx用来搭建Web服务器，负责处理用户请求；fastDFS Tracker 负责管理连接到自身的存储服务器Storage,以及备份各个Storage上的数据；Storage负责存放用户上传的数据资料，以组(group)为单位，每组有1个主存储服务器，同时带有多个备份服务器，数据备份由fastDFS Tracker自动完成。
![总体架构图|center|400*300](http://i4.buimg.com/567571/db2373d58fe9ee89.png)
![总体架构图2|center](http://i1.buimg.com/567571/5405ef839671d15e.png)

### 2.项目逻辑
本项目利用Nginx来搭建Web服务器，处理用户的上传和下载请求，在Nginx服务器上部署fastDFS Client来负责同之后的tracker以及storage通信。
fastDFS Tracker的任务是负责查询和管理连接到自身的fastDFS Storage,当有fastDFS Client需要上传文件时，询问fastDFS Tracker，该文件该传入哪个Storage，Tracker自动判断哪个Storage上的带宽最适合，就将该Storage的地址告诉Client，Client将数据流不经Tracker直接发送给目的Storage，上传完成后Storage返回给Client一个文件存储的路径
