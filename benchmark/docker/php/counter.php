<?php
// Shared by both PHP targets (nginx+php-fpm and Apache+mod_php) in the
// "scenario 3" benchmark (see ../../../ReadMe.md's Benchmarks section):
// read an integer out of a file, increment it, write it back, echo the new
// value. flock() serializes concurrent workers/processes the same way
// nhttp's counter benchmark (../nhttp/bench_counter_main.cpp) serializes
// concurrent threads with a mutex -- both sides pay for correctness under
// concurrency, not just raw I/O.
$file = getenv('COUNTER_FILE') ?: '/tmp/counter.txt';

$fp = fopen($file, 'c+');
if ($fp === false) {
    http_response_code(500);
    echo 'error: cannot open counter file';
    exit(1);
}

flock($fp, LOCK_EX);

$contents = stream_get_contents($fp);
$n = ((int)trim($contents)) + 1;

rewind($fp);
ftruncate($fp, 0);
fwrite($fp, (string)$n);
fflush($fp);

flock($fp, LOCK_UN);
fclose($fp);

echo $n;
