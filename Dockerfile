# 使用 CentOS 7.9 作为基础镜像
FROM centos:7.9.2009

ARG SHARD_DB # root_db, shard_db_3, shard_db_4 etc.

RUN curl -o /etc/yum.repos.d/CentOS-Base.repo https://mirrors.aliyun.com/repo/Centos-7.repo && \
    yum clean all && \
    yum makecache

# 安装开发工具和依赖包
RUN yum groupinstall -y "Development Tools" && \
    yum install -y glibc-devel glibc-headers && \
    yum install -y openssl-devel && \
    yum install -y sshpass && \
    yum clean all

# 设置工作目录
COPY . /root
WORKDIR /root/node

RUN if [ -d "./$SHARD_DB" ]; then \
        mv ./$SHARD_DB ./db; \
    else \
        echo "Error: ./$SHARD_DB does not exist!" && exit 1; \
    fi

# 创建一个默认的命令来查看系统状态
CMD ["./zjchain", "-f", "0", "-g", "0"]
