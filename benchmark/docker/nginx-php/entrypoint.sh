#!/bin/sh
set -e

php-fpm83 -F &
exec nginx -g 'daemon off;'
