FROM debian:buster
RUN apt-get update && apt-get install -y apt-transport-https ca-certificates

## **** If you are from China, please consider using tsinghua mirror **** ## 
RUN echo "deb https://mirrors.tuna.tsinghua.edu.cn/debian/ buster main contrib non-free\n\
	deb-src https://mirrors.tuna.tsinghua.edu.cn/debian/ buster main contrib non-free\n\
	deb https://mirrors.tuna.tsinghua.edu.cn/debian/ buster-updates main contrib non-free\n\
	deb-src https://mirrors.tuna.tsinghua.edu.cn/debian/ buster-updates main contrib non-free\n\
	deb https://mirrors.tuna.tsinghua.edu.cn/debian/ buster-backports main contrib non-free\n\
	deb-src https://mirrors.tuna.tsinghua.edu.cn/debian/ buster-backports main contrib non-free\n\
	deb https://mirrors.tuna.tsinghua.edu.cn/debian-security buster/updates main contrib non-free\n\
	deb-src https://mirrors.tuna.tsinghua.edu.cn/debian-security buster/updates main contrib non-free" > /etc/apt/sources.list

RUN apt-get update && apt-get install -y \
    wget \
    git \
    gcc \
    build-essential \
    libreadline-dev \
    zlib1g-dev \
    bison \
    flex \
    gdb \
    libssl-dev\
    libbz2-dev \
    libsqlite3-dev\
    llvm \
    libncurses5-dev \
    libncursesw5-dev \
    xz-utils \
    libffi-dev \
    liblzma-dev \
    python-openssl

RUN cd /root/ && wget https://www.python.org/ftp/python/3.8.16/Python-3.8.16.tgz

RUN cd /root/ && tar -xzvf Python-3.8.16.tgz && cd Python-3.8.16 && ./configure --enable-optimizations && make install && cd .. && rm -rf ./Python-3.8.16

# Download postgresql-13.1, or copy downloaded into image
RUN cd /root/ && wget https://ftp.postgresql.org/pub/source/v13.1/postgresql-13.1.tar.bz2 --no-check-certificate
# COPY ./postgresql-13.1.tar.bz2 /root/postgresql-13.1.tar.bz2

# Compile postgresql-13.1
RUN cd /root/ \
    && tar xvf postgresql-13.1.tar.bz2  \
    && rm postgresql-13.1.tar.bz2 \
    && cd postgresql-13.1 \
    && ./configure --prefix=/usr/local/pgsql/13.1 --enable-depend --enable-cassert --enable-debug CFLAGS="-ggdb -O0" \
    && make \
    && make install \
    && echo 'export PATH=/usr/local/pgsql/13.1/bin:$PATH' >> /root/.bashrc \
    && echo 'export LD_LIBRARY_PATH=/usr/local/pgsql/13.1/lib/:$LD_LIBRARY_PATH' >> /root/.bashrc

ENV PATH $PATH:/usr/local/pgsql/13.1/bin
ENV LD_LIBRARY_PATH $LD_LIBRARY_PATH:/usr/local/pgsql/13.1/lib/

RUN set -eux \
	&& groupadd -r postgres --gid=999 \
	&& useradd -r -g postgres --uid=999 --home-dir=/var/lib/pgsql/13.1/data --shell=/bin/bash postgres \
	&& mkdir -p /var/lib/pgsql/13.1/data \
	&& chown -R postgres:postgres /var/lib/pgsql/13.1/data \
    && echo 'postgres' > /var/lib/pgsql/13.1/passwd \
    && chmod -R 777 /var/lib/pgsql/13.1/passwd

# for baihe_lib
RUN pip3 install ipython-sql==0.5.0 SQLAlchemy==2.0.2 psycopg2 pandas ipykernel -i https://pypi.tuna.tsinghua.edu.cn/simple

RUN echo "root:root" | chpasswd
USER postgres

RUN initdb -D /var/lib/pgsql/13.1/data --username="postgres" --pwfile="/var/lib/pgsql/13.1/passwd"\
    && echo "host all all all md5" >> /var/lib/pgsql/13.1/data/pg_hba.conf\
    && echo "listen_addresses = '*'" >> /var/lib/pgsql/13.1/data/postgresql.conf\
    && sed -i 's/max_wal_size = 1GB/max_wal_size = 50GB/g' /var/lib/pgsql/13.1/data/postgresql.conf\
    && sed -i 's/shared_buffers = 128MB/shared_buffers = 4GB/g' /var/lib/pgsql/13.1/data/postgresql.conf\
    && echo "geqo = off" >> /var/lib/pgsql/13.1/data/postgresql.conf\
    && echo "max_parallel_workers = 0" >> /var/lib/pgsql/13.1/data/postgresql.conf\
    && echo "max_parallel_workers_per_gather = 0" >> /var/lib/pgsql/13.1/data/postgresql.conf\
    && echo "log_min_messages = info" >> /var/lib/pgsql/13.1/data/postgresql.conf\
    && echo "shared_preload_libraries = 'pilotscope'" >> /var/lib/pgsql/13.1/data/postgresql.conf

USER root
CMD ["bash"]
# CMD ["postgres", "-D", "/var/lib/pgsql/13.1/data"]