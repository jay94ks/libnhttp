FROM node:20-alpine

# build context is benchmark/docker/ (see ../docker-compose.yml).
COPY node-counter/counter.js /app/counter.js

EXPOSE 8080
ENTRYPOINT ["node", "/app/counter.js"]
