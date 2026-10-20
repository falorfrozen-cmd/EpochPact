"""An existing screenshot must never be mistaken for the requested frame."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image
from tools.ingame_capture import capture


class IngameCaptureTests(unittest.TestCase):
    def test_waits_for_a_new_complete_image_instead_of_copying_the_old_file(self):
        with tempfile.TemporaryDirectory() as temp:
            game=Path(temp);report=game/'EpochPact/reports/owned';report.mkdir(parents=True)
            source=report/'screenshot.png';destination=game/'verified.png'
            Image.new('RGB',(2,2),'red').save(source);os.utime(source,ns=(10,10))
            def write_fresh(_):
                Image.new('RGB',(2,2),'green').save(source);os.utime(source,ns=(200,200))
            with patch('tools.ingame_capture.time.time_ns',return_value=100),patch('tools.ingame_capture.time.sleep',side_effect=write_fresh) as wait:
                capture(lambda _:dict(folder=str(report)),game,destination)
                wait.assert_called_once()
            with Image.open(destination) as image:self.assertEqual(image.getpixel((0,0)),(0,128,0))

    def test_rejects_a_report_outside_the_owned_game_report_folder(self):
        with tempfile.TemporaryDirectory() as temp:
            game=Path(temp)
            with self.assertRaisesRegex(RuntimeError,'Unexpected report'):
                capture(lambda _:dict(folder=str(game/'elsewhere')),game,game/'never.png')
            self.assertFalse((game/'never.png').exists())
