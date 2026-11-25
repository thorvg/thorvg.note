import os
import re
import sys


source_root, *command = sys.argv[1:]
separator = re.escape(os.sep)
# Build the filter at runtime because Meson converts backslashes in target arguments.
source_filter = rf'^{re.escape(source_root)}{separator}(src|tests){separator}'
os.execv(command[0], [*command, source_filter])
