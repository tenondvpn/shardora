###############################################################################
# coding: utf-8
#
###############################################################################
"""

Authors: xielei
"""

import os
import tempfile
import time
import logging


class NoBlockSysCommand(object):
    def __init__(self):
        self.__log = logging

    def run_many(self, cmd, retry_times=5):
        stdout = None
        stderr = None
        return_code = -1
        for i in range(retry_times):
            stdout, stderr, return_code = self.run_once(cmd)
            if return_code != 0:
                self.__log.warn('[ERROR]. run cmd [%s] failed! retry %s.' % \
                        (cmd, i + 1))
                time.sleep(1)
            else:
                return stdout, stderr, return_code
        return stdout, stderr, return_code
            
    def run_once(self, cmd):
        stdout_file = tempfile.NamedTemporaryFile()
        stderr_file = tempfile.NamedTemporaryFile()
        cmd = '%(cmd)s 1>>%(out)s 2>>%(err)s' % {
                'cmd': cmd, 
                'out': stdout_file.name, 
                'err': stderr_file.name
            }
        return_code = os.system(cmd)
        # the returnCode of os.system() is encoded by the wait(),
        # it is a 16-bit number, the higher byte is the exit code of the cmd
        # and the lower byte is the signal number to kill the process
        stdout = stdout_file.read()
        stderr = stderr_file.read()
        stdout_file.close()
        stderr_file.close()
        return stdout, stderr, return_code

    def run_in_background(self, cmd, out_filename, err_filename):
        cmd = '%(cmd)s 1>%(out)s 2>%(err)s &' % {
                'cmd': cmd,
                'out': out_filename,
                'err': err_filename 
            }
        return os.system(cmd)

    def run_in_console(self, cmd):
        cmd = '%(cmd)s 1>>%(out)s 2>>%(err)s' % {
                'cmd': cmd,
                'out': '/dev/null',
                'err': '/dev/null'
            }
        return_code = os.system(cmd)

if __name__ == '__main__':
    process = NoBlockSysCommand()
    cmd = ("/apsara/deploy/rpc_caller "
            "--Server=nuwa://localcluster/sys/fuxi/master/ForChildMaster "
            "--Method=GetWorkItemStatus "
            "--Parameter=nuwa://localcluster/data_platform/job_334293/"
            "JobMaster")
    print(process.run_many(cmd))
