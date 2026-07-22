<?php
require __DIR__ . '/config.php';

header('Content-Type: application/json; charset=utf-8');
header('Cache-Control: no-store, no-cache, must-revalidate, max-age=0');

function findLatestMeasurement($data) {
    if (!is_array($data)) return null;

    if (array_key_exists('battery', $data) || array_key_exists('value', $data)) {
        return $data;
    }

    if (isset($data['measurements']) && is_array($data['measurements'])) {
        $items = array();

        foreach ($data['measurements'] as $series) {
            if (!is_array($series)) continue;

            foreach ($series as $item) {
                if (is_array($item)) $items[] = $item;
            }
        }

        if (count($items) > 0) {
            usort($items, function($a, $b) {
                return strcmp(
                    isset($b['timestamp']) ? $b['timestamp'] : '',
                    isset($a['timestamp']) ? $a['timestamp'] : ''
                );
            });

            return $items[0];
        }
    }

    foreach ($data as $value) {
        $result = findLatestMeasurement($value);
        if ($result !== null) return $result;
    }

    return null;
}

function fetchUrl($url) {
    if (function_exists('curl_init')) {
        $curl = curl_init($url);

        curl_setopt($curl, CURLOPT_RETURNTRANSFER, true);
        curl_setopt($curl, CURLOPT_FOLLOWLOCATION, true);
        curl_setopt($curl, CURLOPT_CONNECTTIMEOUT, REQUEST_TIMEOUT);
        curl_setopt($curl, CURLOPT_TIMEOUT, REQUEST_TIMEOUT);
        curl_setopt($curl, CURLOPT_USERAGENT, 'ETO Dashboard HMI/1.0');
        curl_setopt($curl, CURLOPT_SSL_VERIFYPEER, true);
        curl_setopt($curl, CURLOPT_SSL_VERIFYHOST, 2);

        $body = curl_exec($curl);
        $code = (int)curl_getinfo($curl, CURLINFO_HTTP_CODE);
        $error = curl_error($curl);
        curl_close($curl);

        if ($body === false) throw new Exception('Błąd cURL: ' . $error);
        if ($code < 200 || $code >= 300) throw new Exception('HTTP ' . $code);

        return $body;
    }

    if (!ini_get('allow_url_fopen')) {
        throw new Exception('Brak cURL i wyłączone allow_url_fopen.');
    }

    $context = stream_context_create(array(
        'http' => array(
            'timeout' => REQUEST_TIMEOUT,
            'header' => "User-Agent: ETO Dashboard HMI/1.0\r\n"
        )
    ));

    $body = @file_get_contents($url, false, $context);

    if ($body === false) throw new Exception('Nie udało się pobrać danych.');

    return $body;
}

$result = array();

foreach ($DEVICES as $device) {
    $source = isset($device['source']) ? $device['source'] : '';
    $name = isset($device['name']) ? $device['name'] : $source;
    $url = sprintf(BASE_URL, rawurlencode($source));

    try {
        $body = fetchUrl($url);
        $decoded = json_decode($body, true);

        if ($decoded === null && json_last_error() !== JSON_ERROR_NONE) {
            throw new Exception('Nieprawidłowy JSON: ' . json_last_error_msg());
        }

        $sample = findLatestMeasurement($decoded);

        if (!is_array($sample)) {
            throw new Exception('Brak pomiarów value/battery.');
        }

        $result[] = array(
            'id' => isset($device['id']) ? $device['id'] : null,
            'source' => $source,
            'name' => $name,
            'value' => isset($sample['value']) ? $sample['value'] : null,
            'battery' => isset($sample['battery']) ? $sample['battery'] : null,
            'timestamp' => isset($sample['timestamp']) ? $sample['timestamp'] : null
        );

    } catch (Exception $e) {
        $result[] = array(
            'id' => isset($device['id']) ? $device['id'] : null,
            'source' => $source,
            'name' => $name,
            'value' => null,
            'battery' => null,
            'timestamp' => null,
            'error' => $e->getMessage()
        );
    }
}

echo json_encode($result, JSON_UNESCAPED_UNICODE | JSON_UNESCAPED_SLASHES);
