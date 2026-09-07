FROM node:20-alpine

# build context is benchmark/docker/ (see ../docker-compose.yml).
COPY node/static_server.js /app/static_server.js
COPY content/index.html /usr/share/node-static/html/index.html

EXPOSE 8080
ENTRYPOINT ["node", "/app/static_server.js", "8080", "/usr/share/node-static/html"]
