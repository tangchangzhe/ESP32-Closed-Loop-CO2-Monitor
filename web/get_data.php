<?php
/**
 * Data Query API
 * 
 * Returns CO2 historical data for visualization.
 * 
 * Parameters:
 *   start - Start datetime (default: 24 hours ago)
 *   end   - End datetime (default: now)
 */

header('Content-Type: application/json; charset=utf-8');
header('Access-Control-Allow-Origin: *');

// Load config
$config = require 'config.php';

// Connect to database
$conn = new mysqli($config['db_host'], $config['db_user'], $config['db_pass'], $config['db_name']);

if ($conn->connect_error) {
    die(json_encode(['error' => 'Database connection failed']));
}

$conn->set_charset("utf8mb4");

// Get time range parameters
$end_time = isset($_GET['end']) ? $_GET['end'] : date('Y-m-d H:i:s');
$start_time = isset($_GET['start']) ? $_GET['start'] : date('Y-m-d H:i:s', strtotime('-1 day'));

// Query data
$sql = "SELECT timestamp, co2_ppm, temperature, humidity 
        FROM co2_measurements 
        WHERE timestamp BETWEEN ? AND ? 
        ORDER BY timestamp ASC";

$stmt = $conn->prepare($sql);
$stmt->bind_param("ss", $start_time, $end_time);
$stmt->execute();
$result = $stmt->get_result();

$data = [];
while ($row = $result->fetch_assoc()) {
    $data[] = $row;
}

echo json_encode($data);

$stmt->close();
$conn->close();
?>
