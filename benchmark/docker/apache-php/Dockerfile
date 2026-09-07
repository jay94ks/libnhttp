FROM php:8.3-apache

# keep-alive tuning, same rationale as ../apache/Dockerfile.
RUN { \
      echo 'KeepAlive On'; \
      echo 'MaxKeepAliveRequests 100000'; \
      echo 'KeepAliveTimeout 65'; \
    } > /etc/apache2/conf-enabled/zz-bench-keepalive.conf

# build context is benchmark/docker/ (see ../docker-compose.yml).
COPY apache-php/mpm_prefork_bench.conf /etc/apache2/conf-enabled/zz-bench-mpm.conf
COPY php/counter.php /var/www/html/counter.php

EXPOSE 80
