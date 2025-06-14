#!/bin/bash

VIDEO_FOLDER="/mnt/zippy/VIDEO/test/City Slickers II: The Legend of Curly's Gold (1994)"
# REVISED: VIDEO_PARENT must be /mnt/zippy/VIDEO/test for sibling checks
VIDEO_PARENT="/mnt/zippy/VIDEO/test"
COPY_VIDEO_FOLDER="${VIDEO_FOLDER} (copy)"

if [ ! -d "${COPY_VIDEO_FOLDER}" ]; then
    echo "Error: '${COPY_VIDEO_FOLDER}' not found." >&2
    exit 1
fi

# Get the count of sibling folders in VIDEO_PARENT, excluding the COPY_VIDEO_FOLDER itself.
# This will count all directories in /mnt/zippy/VIDEO/test/ EXCEPT the copy folder.
TEMP_SIBLING_FOLDERS=$(find "${VIDEO_PARENT}" -maxdepth 1 -type d ! -name "$(basename "${COPY_VIDEO_FOLDER}")" -print)
# Count only actual directories, explicitly excluding the parent directory itself if 'find' lists it.
SIBLING_FOLDER_COUNT=$(echo "$TEMP_SIBLING_FOLDERS" | grep -v "^${VIDEO_PARENT}$" | wc -l)

# If > 1 sibling folder (other than COPY_VIDEO_FOLDER) exists in VIDEO_PARENT, exit with error "Multiple sibling folders"
if [ "${SIBLING_FOLDER_COUNT}" -gt 1 ]; then
    echo "Error: Multiple sibling folders found in '${VIDEO_PARENT}', excluding '${COPY_VIDEO_FOLDER}'." >&2
    exit 1
fi

# If ANY sibling FILES exist in VIDEO_PARENT (alongside the folders), exit with error "Invalid sibling file(s)"
SIBLING_FILES_COUNT=$(find "${VIDEO_PARENT}" -maxdepth 1 -type f | wc -l)
if [ "${SIBLING_FILES_COUNT}" -gt 0 ]; then
    echo "Error: Invalid sibling file(s) found in '${VIDEO_PARENT}'." >&2
    exit 1
fi

# Delete the single sibling folder if it exists (the one that is not the copy).
# This finds the single non-copy folder in VIDEO_PARENT.
NON_COPY_SIBLING_FOLDER=$(find "${VIDEO_PARENT}" -maxdepth 1 -type d ! -name "$(basename "${COPY_VIDEO_FOLDER}")" ! -name "$(basename "${VIDEO_PARENT}")" -print | head -n 1)

# Only attempt deletion if such a folder was found and it's a directory
if [ -n "$NON_COPY_SIBLING_FOLDER" ] && [ -d "$NON_COPY_SIBLING_FOLDER" ]; then
    rm -rf "${NON_COPY_SIBLING_FOLDER}"
fi

# Copy the contents of the '(copy)' folder to the original folder's location
# The -a option is crucial: it archives files, preserving most attributes (permissions, timestamps, etc.)
cp -a "${COPY_VIDEO_FOLDER}" "${VIDEO_FOLDER}"


# --- Replacement 2: Localtest Folder ---
# Original folder path (will be replaced)
ORIGINAL_LOCALTEST_FOLDER="/home/steve/Cloud/localtest"

# Source folder path (the 'copy' version)
COPY_LOCALTEST_FOLDER="${ORIGINAL_LOCALTEST_FOLDER} (copy)"

# Remove the original folder if it exists
rm -rf "${ORIGINAL_LOCALTEST_FOLDER}"

# Copy the contents of the '(copy)' folder to the original folder's location
cp -a "${COPY_LOCALTEST_FOLDER}" "${ORIGINAL_LOCALTEST_FOLDER}"
