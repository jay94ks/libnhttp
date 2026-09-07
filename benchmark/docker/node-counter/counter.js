// Node.js side of the "scenario 3" benchmark (see ../../../ReadMe.md's
// Benchmarks section): the same read-integer/increment/write-back/respond
// endpoint as ../nhttp/bench_counter_main.cpp (nhttp) and ../php/counter.php
// (PHP under nginx/Apache), implemented with only Node's built-in `http`
// and `fs` modules -- no framework, no third-party dependencies, matching
// how minimally the other two sides are implemented.
//
// Deliberately synchronous fs calls (readFileSync/writeFileSync), not
// fs.promises: Node is single-threaded, and the async fs APIs would let two
// concurrent requests interleave between their read and their write (lost
// updates), the same race the mutex in bench_counter_main.cpp and the
// flock() in counter.php both exist to prevent. Synchronous calls block
// Node's one event-loop thread for the duration of the file I/O, which
// serializes the critical section for free -- the same correctness
// guarantee, just via blocking the only thread there is instead of an
// explicit lock. This is also a single, unclustered `node` process (no
// `cluster` module) -- the common single-instance deployment, matching one
// nhttp process and directly comparable to it; it does NOT spread across
// multiple cores the way PHP-FPM/Apache's multi-process model or nhttp's
// multi-threaded reactor do, which this benchmark's numbers should be read
// with in mind.
const http = require('http');
const fs = require('fs');

const PORT = process.env.PORT || 8080;
const COUNTER_FILE = process.env.COUNTER_FILE || '/tmp/counter.txt';

const server = http.createServer((req, res) => {
	let n = 0;

	try {
		n = parseInt(fs.readFileSync(COUNTER_FILE, 'utf8'), 10) || 0;
	} catch (err) {
		n = 0;
	}

	n += 1;
	fs.writeFileSync(COUNTER_FILE, String(n));

	res.writeHead(200, { 'Content-Type': 'text/plain' });
	res.end(String(n));
});

server.keepAliveTimeout = 65000;
server.listen(PORT, '0.0.0.0', () => {
	console.log(`node counter-bench server listening on 0.0.0.0:${PORT}, counter file '${COUNTER_FILE}'`);
});
