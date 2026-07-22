<?php
define('REFRESH_SECONDS', 15);
define('REQUEST_TIMEOUT', 8);
define('OFFLINE_AFTER_SECONDS', 3600);

define(
    'BASE_URL',
    'https://dlb.com.pl/api/tlm/v2/data/telemetry_%s.json'
);

$DEVICES = array(
    array('id'=>1,  'source'=>'ETOANDONBECZKA1',  'name'=>'MPK40190'),
    array('id'=>2,  'source'=>'ETOANDONBECZKA2',  'name'=>'MPK40200'),
    array('id'=>3,  'source'=>'ETOANDONBECZKA3',  'name'=>'MPK40XXX'),
    array('id'=>4,  'source'=>'ETOANDONBECZKA4',  'name'=>'MPK40XXX'),
    array('id'=>5,  'source'=>'ETOANDONBECZKA5',  'name'=>'MPK40XXX'),
    array('id'=>6,  'source'=>'ETOANDONBECZKA6',  'name'=>'MPK40XXX'),
    array('id'=>7,  'source'=>'ETOANDONBECZKA7',  'name'=>'MPK40XXX'),
    array('id'=>8,  'source'=>'ETOANDONBECZKA8',  'name'=>'MPK40XXX'),
    array('id'=>9,  'source'=>'ETOANDONBECZKA9',  'name'=>'MPK40XXX'),
    array('id'=>10, 'source'=>'ETOANDONBECZKA10', 'name'=>'MPK40XXX')
    //array('id'=>11, 'source'=>'ETOANDONBECZKA11', 'name'=>'MPK40200'),
    //array('id'=>12, 'source'=>'ETOANDONBECZKA12', 'name'=>'MPK40201')
);
