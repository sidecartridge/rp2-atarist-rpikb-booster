#!/bin/bash
# upload_s3.sh
#
# This script reads AWS_SECRET_ACCESS_KEY and AWS_ACCESS_KEY_ID from the environment
# and uploads a file (passed as an argument) to the S3 bucket at:
#   s3://atarist.sidecartridge.com/
# It overwrites any previous content at that location.

# Check for required AWS environment variables.
if [ -z "$AWS_SECRET_ACCESS_KEY" ] || [ -z "$AWS_ACCESS_KEY_ID" ]; then
    echo "Error: AWS_SECRET_ACCESS_KEY and AWS_ACCESS_KEY_ID must be set in the environment."
    exit 1
fi

# Check that exactly one argument is provided.
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <file-to-upload>"
    exit 1
fi

FILE_TO_UPLOAD="$1"

# Verify the file exists.
if [ ! -f "$FILE_TO_UPLOAD" ]; then
    echo "Error: File '$FILE_TO_UPLOAD' does not exist."
    exit 1
fi

# Build the destination S3 URL using the file's basename.
DESTINATION="s3://atarist.sidecartridge.com/$(basename "$FILE_TO_UPLOAD")"

# Upload the file using AWS CLI.
# The 'aws s3 cp' command by default overwrites the destination if it exists.
echo "Uploading '$FILE_TO_UPLOAD' to '$DESTINATION'..."
aws s3 cp "$FILE_TO_UPLOAD" "$DESTINATION"

if [ "$?" -ne 0 ]; then
    echo "Error: Upload failed."
    exit 1
fi

echo "File uploaded successfully to $DESTINATION."
exit 0
