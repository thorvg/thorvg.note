import os
from pathlib import Path
import subprocess
import sys


bundle = Path(sys.argv[1])
destdir = os.environ.get("DESTDIR")
if destdir:
    bundle = Path(destdir) / bundle.relative_to(bundle.anchor)

subprocess.run([sys.argv[2], "--force", "--sign", "-", str(bundle)], check=True)
