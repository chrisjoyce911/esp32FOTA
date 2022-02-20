<?php
/*
use it like http://firmware.host.au/project.json?id=0815

*/

function xss_cleaner($input_str)
{
    $return_str = str_replace(array('<', ';', '|', '&', '>', "'", '"', ')', '('), array('&lt;', '&#58;', '&#124;', '&#38;', '&gt;', '&apos;', '&#x22;', '&#x29;', '&#x28;'), $input_str);
    $return_str = str_ireplace('%3Cscript', '', $return_str);
    return $return_str;
}


header('Content-Type: application/json; charset=utf-8');

mysqli_report(MYSQLI_REPORT_ERROR | MYSQLI_REPORT_STRICT);

$conn = new mysqli("localhost", "admin", "Password", "FOTA_firmware");

$host = $_SERVER['SERVER_NAME'];
$jsonurl = explode('.js', $_SERVER['REQUEST_URI']);
$fileurl = explode('.', $_SERVER['REQUEST_URI']);
$json = substr($jsonurl[0], 1);

if (isset($jsonurl[1]) && $jsonurl[1] != '') {
    if (array_key_exists('id', $_GET)) {
        $id = $_GET['id'];
    } else {
        $id = '';
    }
    $jsonparam = xss_cleaner($json);
    if ($id == '') {
        $query = "SELECT `firmware_id`, `version`, `private`, `firmware_type` FROM `firmware` WHERE `private` = '0' AND `firmware_type` = '$jsonparam' ORDER BY `version` DESC";
        $stmt = $conn->query($query);
    } else {
        $id = xss_cleaner($id);
        $query = "
      SELECT
      `devices`.`firmware_id`,
      `firmware`.`version`,
      `firmware`.`private`,
      `firmware`.`firmware_type`

      FROM
      `devices`

      JOIN `firmware` USING(`firmware_id`)

      WHERE
      `device_id` = '$id' ORDER BY `version` DESC
      ";
        $stmt = $conn->query($query);
    }
    //$dbresult  = $stmt->fetchAll('assoc');
    if ($stmt->num_rows == 0) { // $stmt->rowCount()
        $query = "SELECT `firmware_id`, `version`, `private`, `firmware_type` FROM `firmware` WHERE `private` = '0' AND `firmware_type` = '$jsonparam' ORDER BY `version` DESC";
        $stmt = $conn->query($query);
    }
    $row  = $stmt->fetch_array(MYSQLI_ASSOC);
    $response = array();
    $response['type'] = $row['firmware_type'];
    $response['version'] = $row['version'];
    $response['host'] = $host;
    $response['port'] = '80';
    $response['bin'] = "/" . $row['firmware_type'] . '.' . $row['firmware_id'] . '.bin';
    $jsonresponse = json_encode($response);
    $jsonresponse = str_replace("\/", "/", $jsonresponse);
    header('Content-Type: application/json');
    print $jsonresponse;
    exit;
}


if (isset($fileurl[2]) && $fileurl[2] == 'bin') {
    $fileparam = xss_cleaner($fileurl[1]);
    $fileparam = $fileparam * 1;
    $query = "
    SELECT
    `id`,
    `version`,
    `firmware_type`,
    `payload`,
    OCTET_LENGTH(payload) as size

    FROM
    `firmware`

    WHERE
    `firmware_id` = '$fileparam'
    ";
    $stmt = $conn->query($query);
    $file  = $stmt->fetch_array(MYSQLI_ASSOC);
    header('Content-Description: File Transfer');
    header('Content-Type: application/octet-stream');
    header('Content-Length: ' . $file['size']);
    header('Content-Disposition: attachment; filename="' . $file['firmware_type'] . '.' . $file['id'] . '.bin"');
    header('Content-Transfer-Encoding: binary');
    header('Expires: 0');
    header('Cache-Control: must-revalidate, post-check=0, pre-check=0');
    header('Pragma: public');
    echo $file['payload'];
    exit;
}
