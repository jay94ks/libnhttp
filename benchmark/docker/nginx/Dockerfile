FROM nginx:1.24-alpine

# build context is benchmark/docker/ (see ../docker-compose.yml), so paths
# below are relative to that, not this Dockerfile's own directory.
COPY nginx/nginx.conf /etc/nginx/nginx.conf
COPY content/index.html /usr/share/nginx/html/index.html

EXPOSE 80
