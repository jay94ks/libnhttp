FROM nginx:1.27-alpine

RUN apk add --no-cache php83 php83-fpm

# build context is benchmark/docker/ (see ../docker-compose.yml).
COPY nginx-php/nginx.conf /etc/nginx/nginx.conf
COPY nginx-php/www.conf /etc/php83/php-fpm.d/www.conf
COPY nginx-php/entrypoint.sh /entrypoint.sh
COPY php/counter.php /usr/share/nginx/html/counter.php

RUN chmod +x /entrypoint.sh \
    && mkdir -p /tmp && chmod 1777 /tmp

EXPOSE 80
ENTRYPOINT ["/entrypoint.sh"]
