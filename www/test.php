<?php
header('Content-Type: text/plain; charset=utf-8');

echo "PHP działa\n";
echo "Wersja PHP: " . PHP_VERSION . "\n";
echo "cURL: " . (function_exists('curl_init') ? 'TAK' : 'NIE') . "\n";
echo "allow_url_fopen: " . (ini_get('allow_url_fopen') ? 'TAK' : 'NIE') . "\n";
