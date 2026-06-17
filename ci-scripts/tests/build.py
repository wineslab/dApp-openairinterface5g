# SPDX-License-Identifier: LicenseRef-CSSL-1.0

import sys
import logging
logging.basicConfig(
	level=logging.DEBUG,
	stream=sys.stdout,
	format="[%(asctime)s] %(levelname)8s: %(message)s"
)
import unittest
import tempfile

sys.path.append('./') # to find OAI imports below
import cls_oai_html
import cls_containerize
from cls_ci_helper import TestCaseCtx
import cls_cmd

class TestBuild(unittest.TestCase):
	def setUp(self):
		self.html = cls_oai_html.HTMLManagement()
		self.html.testCaseIdx = "000000"
		self.cont = cls_containerize.Containerize()
		self._d = tempfile.mkdtemp()
		logging.warning(f"temporary directory: {self._d}")
		self.node = 'localhost'
		self.cont.workspace = self._d
		self.ctx = TestCaseCtx.Default(tempfile.mkdtemp())

	def tearDown(self):
		logging.warning(f"removing directory contents")
		with cls_cmd.getConnection(None) as cmd:
			cmd.run(f"rm -rf {self._d}")
			cmd.run(f'rm -rf {self.ctx.logPath}')

if __name__ == '__main__':
	unittest.main()
