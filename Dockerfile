
# 使用 CentOS 7.9 作为基础镜像
FROM centos:7.9.2009

RUN curl -o /etc/yum.repos.d/CentOS-Base.repo https://mirrors.aliyun.com/repo/Centos-7.repo && \
    yum clean all && \
    yum makecache

# 安装开发工具和依赖包
RUN yum groupinstall -y "Development Tools" && \
    yum install -y glibc-devel glibc-headers && \
    yum install -y openssl-devel && \
    yum install -y sshpass && \
    yum clean all

RUN mkdir -p /root/shardora/cbuild_Debug
RUN mkdir -p /root/shardora/zjnodes_local
# 设置工作目录
COPY ./cbuild_Debug/zjchain /root/shardora/cbuild_Debug/zjchain
COPY ./zjnodes_local /root/shardora/zjnodes_local
COPY ./docker_simple_dep.sh /root/shardora/
WORKDIR /root/shardora

# 创建一个默认的命令来查看系统状态
CMD ["sh", "docker_simple_dep.sh 4"]
