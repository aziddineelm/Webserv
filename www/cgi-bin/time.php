#!/usr/bin/env php-cgi
<?php
header('Content-Type: application/json');
echo json_encode([
    'status' => 'success',
    'time' => date('Y-m-d H:i:s'),
    'timestamp' => time()
], JSON_PRETTY_PRINT);
?>
