"""Wait for a fresh, complete Unity capture; an existing filename is not proof."""
from pathlib import Path
import shutil
import time
from PIL import Image


def capture(request, game, destination):
    before=time.time_ns()
    report=request('reportnow')
    source=Path(report['folder'])/'screenshot.png'
    expected=(Path(game)/'EpochPact/reports').resolve()
    if source.parent.resolve().parent!=expected:raise RuntimeError('Unexpected report path')
    deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        try:
            if source.stat().st_mtime_ns>=before:
                with Image.open(source) as image:image.verify()
                shutil.copyfile(source,destination)
                return source
        except (OSError,ValueError,SyntaxError):pass
        time.sleep(.05)
    raise RuntimeError('No fresh, complete screenshot was written')
