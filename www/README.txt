DASHBOARD HMI PHP

Wgraj wszystkie pliki do jednego katalogu na serwerze.

Pliki:
- index.php
- api.php
- config.php
- test.php

Konfiguracja nazw i źródeł jest w config.php.

Przykład:
array(
    'id'=>1,
    'source'=>'ETOANDONBECZKA1',
    'name'=>'MPK40190'
)

source = nazwa pliku bez telemetry_ i bez .json
name   = dowolna nazwa widoczna na kafelku

Parametry:
define('REFRESH_SECONDS', 5);
define('OFFLINE_AFTER_SECONDS', 60);

OFFLINE pojawia się, gdy:
- nie udało się pobrać danych,
- brak timestamp,
- pomiar jest starszy niż OFFLINE_AFTER_SECONDS.

Test:
1. test.php
2. api.php
3. index.php
