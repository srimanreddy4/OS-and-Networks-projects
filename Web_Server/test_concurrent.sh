#!/bin/bash

# Number of concurrent requests to send
NUM_REQUESTS=10

echo "Sending $NUM_REQUESTS concurrent requests..."

# Loop and launch curl commands in the background
for i in $(seq 1 $NUM_REQUESTS)
do
   # The '&' sends the command to the background
   curl http://localhost:8080 &
done

echo "Waiting for requests to finish..."
# 'wait' pauses the script until all background jobs launched by this script complete
wait

echo "All requests completed."