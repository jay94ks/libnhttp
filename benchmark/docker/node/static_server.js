// Node.js side of the static-file benchmarks (see ../../../ReadMe.md's
// Benchmarks section, "Loopback" and "Docker network-stack" scenarios):
// serves the same deterministic 10 KB `content/index.html` nginx/Apache/
// nhttpd serve, using only Node's built-in `http`/`fs` modules -- no
// framework, matching how minimally the other benchmark harnesses
// (bench_main.cpp, counter.js) are implemented.
//
// Deliberately `fs.createReadStream(...).pipe(res)` per request, not a
// startup-time in-memory Buffer: nginx/Apache/nhttpd all serve this file via
// sendfile(2), which still pays a real per-request read (just one that's
// page-cache-backed after the first hit) rather than skipping the read
// entirely -- reading fresh from the filesystem on every request keeps this
// a comparable cost, not an unfair shortcut.
//
// Run directly (loopback benchmark): `node static_server.js [port] [dir]`
// Run in Docker (see ./Dockerfile): args come from ENTRYPOINT instead.
const http = require('http');
const fs = require('fs');
const path = require('path');

const PORT = process.argv[2] ? parseInt(process.argv[2], 10) : (process.env.PORT || 8080);
const ROOT = process.argv[3] || process.env.STATIC_ROOT || '/usr/share/node-static/html';
const FILE = path.join(ROOT, 'index.html');

const server = http.createServer((req, res) => {
	const stream = fs.createReadStream(FILE);

	stream.on('error', () => {
		res.writeHead(404);
		res.end();
	});

	res.writeHead(200, { 'Content-Type': 'text/html' });
	stream.pipe(res);
});

server.keepAliveTimeout = 65000;
server.listen(PORT, '0.0.0.0', () => {
	console.log(`node static-bench server listening on 0.0.0.0:${PORT}, serving '${FILE}'`);
});
