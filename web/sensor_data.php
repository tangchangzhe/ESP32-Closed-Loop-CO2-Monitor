<?php
/**
 * Sensor Data Receiver API
 * 
 * Receives JSON data from ESP32 and stores in database.
 * Supports both single record and batch upload.
 */

header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');
header('Access-Control-Allow-Methods: POST, OPTIONS');
header('Access-Control-Allow-Headers: Content-Type');

// Handle preflight request
if ($_SERVER['REQUEST_METHOD'] == 'OPTIONS') {
    http_response_code(200);
    exit;
}

// Only accept POST
if ($_SERVER['REQUEST_METHOD'] != 'POST') {
    http_response_code(405);
    echo json_encode(['status' => 'error', 'message' => 'Only POST method allowed']);
    exit;
}

// Load config
$config = require 'config.php';

// Read JSON data
$json = file_get_contents('php://input');
if (empty($json)) {
    echo json_encode(['status' => 'error', 'message' => 'No data received']);
    exit;
}

$data = json_decode($json, true);
if (!$data) {
    echo json_encode(['status' => 'error', 'message' => 'Invalid JSON format']);
    exit;
}

// Connect to database
$conn = new mysqli($config['db_host'], $config['db_user'], $config['db_pass'], $config['db_name']);

if ($conn->connect_error) {
    echo json_encode(['status' => 'error', 'message' => 'Database connection failed']);
    exit;
}

$conn->set_charset("utf8mb4");

// Prepare SQL statement
$sql = "INSERT INTO co2_measurements (timestamp, co2_ppm, temperature, humidity) VALUES (?, ?, ?, ?)";
$stmt = $conn->prepare($sql);

if (!$stmt) {
    echo json_encode(['status' => 'error', 'message' => 'SQL preparation failed']);
    $conn->close();
    exit;
}

// Handle batch upload (array) or single record
$records = isset($data[0]) ? $data : [$data];
$inserted = 0;
$errors = [];

foreach ($records as $record) {
    // Validate required fields
    if (!isset($record['timestamp']) || !isset($record['co2'])) {
        $errors[] = 'Missing required fields';
        continue;
    }
    
    $timestamp = $record['timestamp'];
    $co2 = intval($record['co2']);
    $temperature = isset($record['temperature']) ? floatval($record['temperature']) : null;
    $humidity = isset($record['humidity']) ? floatval($record['humidity']) : null;
    
    $stmt->bind_param("sidd", $timestamp, $co2, $temperature, $humidity);
    
    if ($stmt->execute()) {
        $inserted++;
    } else {
        $errors[] = $stmt->error;
    }
}

$stmt->close();
$conn->close();

if ($inserted > 0) {
    echo json_encode([
        'status' => 'success',
        'message' => "Inserted $inserted records",
        'inserted' => $inserted,
        'errors' => $errors
    ]);
} else {
    echo json_encode([
        'status' => 'error',
        'message' => 'No records inserted',
        'errors' => $errors
    ]);
}
?>
