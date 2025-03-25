###############################################################################
# coding: utf-8
#
###############################################################################
"""

Authors: xielei
"""

import os
import shutil
import sys
import string
import re
import socket
import datetime
import no_block_sys_cmd

class FileCommandRet(object):
    FILE_COMMAND_SUCC = 0  # 操作文件成功
    FILE_IS_NOT_EXISTS = 1  # 文件不存在
    FILE_PATH_IS_NOT_EXISTS = 2  # 文件路径不存在（不包括文件名）
    FILE_READ_ERROR = 3  # 读取文件失败
    FILE_WRITE_ERROR = 4  # 写文件失败

class StaticFunction(object):
    @staticmethod
    def strip_with_one_space(in_str):
        """
            将字符串中的\t以及多余空格全部替换为一个空格间隔
        """
        return ' '.join(filter(lambda x: x, in_str.split(' ')))

    @staticmethod
    def strip_with_nothing(in_str):
        """
            将字符串中的\t以及多余空格全部去除
        """
        return ''.join(filter(lambda x: x, in_str.split(' ')))

    @staticmethod
    def get_local_ip():
        """
            获取本地ip地址
        """
        return socket.gethostbyname(socket.gethostname())

    @staticmethod
    def get_all_content_from_file(file_path, param="r"):
        """
            读取文件，并返回所有数据
            file_path： 需要读取的文件路径
            param： 读文件参数
            return: 失败 None 成功：文件内容
        """
        if not os.path.exists(file_path):
            return FileCommandRet.FILE_PATH_IS_NOT_EXISTS, ''

        fd = open(file_path, param)
        try:
            return FileCommandRet.FILE_COMMAND_SUCC, fd.read()
        except Exception as ex:
            return FileCommandRet.FILE_WRITE_ERROR, ''
        finally:
            fd.close()

    @staticmethod
    def get_file_content_with_start_and_len(file_path, start, len, param="r"):
        if not os.path.exists(file_path):
            return FileCommandRet.FILE_PATH_IS_NOT_EXISTS, ''

        fd = open(file_path, param)
        try:
            fd.seek(start)
            return FileCommandRet.FILE_COMMAND_SUCC, fd.read(len)
        except Exception as ex:
            return FileCommandRet.FILE_WRITE_ERROR, ''
        finally:
            fd.close()

    @staticmethod
    def write_content_to_file(file_path, content, param="w"):
        """
            将内容写入文件
            content： 写入内容
            file_path： 文件路径
            param： 写文件参数
            return: FileCommandRet
        """
        path = os.path.dirname(os.path.abspath(file_path))
        if not os.path.exists(path):
            return FileCommandRet.FILE_PATH_IS_NOT_EXISTS

        fd = open(file_path, param)
        try:
            fd.write(content)
            return FileCommandRet.FILE_COMMAND_SUCC
        except Exception as ex:
            return FileCommandRet.FILE_WRITE_ERROR
        finally:
            fd.close()

    @staticmethod
    def replace_str_with_regex(src_content, pattern, kv_pair_map):
        """
            通过正则表达式pattern，将输入内容按照kv_pair_map，替换关键字内容
            src_content: 需要替换的源字符串
            pattern: 匹配的正则表达式
            kv_pair_map: 替换kv对map
            return: 失败: None, 成功：正确替换内容

            Basic Example:
                src_content = 
                    " {
                        "part": "${part}",
                        "sep": "${sep}",
                        "/SetLimit/core": "unlimited",
                        "/SetEnv/LD_LIBRARY_PATH": "/usr/ali/java/jre/",
                        "table": "${table}",
                       }
                    "
                pattern = '\$\{[^}]*\}'
                kv_pair_map = {
                    "part" : "test_part",
                    "sep" : "test_sep",
                }

                print(task_util.StaticFunction.replace_str_with_regex(
                        src_content, 
                        pattern, 
                        kv_pair_map))
                输出：
                    " {
                        "part": "test_part",
                        "sep": "test_sep",
                        "/SetLimit/core": "unlimited",
                        "/SetEnv/LD_LIBRARY_PATH": "/usr/ali/java/jre/",
                        "table": "",  # 如果kv_pair_map中没有此关键字，替换为空
                       }
                    "
        """
        try:
            template = string.Template(src_content)
            tmp_content = template.safe_substitute(kv_pair_map)
            regex_object = re.compile(pattern)
            return regex_object.sub("", tmp_content)
        except Exception as ex:
            return None

    @staticmethod
    def get_path_sub_regex_pattern(full_path, parent_dir):
        if len(full_path) <= len(parent_dir):
            return None
        if parent_dir[-1] != '/':
            parent_dir = parent_dir + '/'
        find_pos = full_path.find("/", len(parent_dir))
        if find_pos == -1:
            return full_path[len(parent_dir): len(full_path)]
        return full_path[len(parent_dir): find_pos]

    @staticmethod
    def get_now_format_time(format):
        now_time = datetime.datetime.utcnow() + datetime.timedelta(hours=8)
        return now_time.strftime(format)

    @staticmethod
    def remove_empty_file(file_path):
        if not os.path.exists(file_path):
            return False

        if os.path.isdir(file_path):
            return False

        if os.path.getsize(file_path) <= 0:
            os.remove(file_path)
        return True
    
class LinuxFileCommand(object):
    """
        linux文件相关操作命令
    """
    def exist_dir(self, dir_path, run_once=True):
        return os.path.exists(dir_path)

    def exist_file(self, file_path, run_once=True):
        return os.path.exists(file_path)

    def read_file(self, file_path, run_once=False):
        ret, content = StaticFunction.get_all_content_from_file(
                file_path)
        if ret != FileCommandRet.FILE_COMMAND_SUCC:
            return None
        return content

    def mk_dir(self, file_dir, run_once=False):
        try:
            os.mkdir(file_dir)
            return True
        except:
            return False

    def mv_file(self, src_file, dest_file):
        try:
            shutil.move(src_file, dest_file)
            return True
        except:
            return False

    def mv_dir(self, src_dir, dest_dir):
        return self.mv_file(src_dir, dest_dir)

    def cp_file(self, src_file, dest_file, run_once=True):
        try:
            shutil.copy(src_file, dest_file)
            return True
        except:
            return False

    def cp_dir(self, src_dir, dest_dir):
        return self.cp_file(src_dir, dest_dir)

    def rm_file(self, file_name, run_once=False):
        try:
            shutil.rmtree(file_name)
            return True
        except:
            return False

    def rm_dir(self, dir_name, run_once=False):
        return self.rm_file(dir_name)

    def ls_dir(self, dir_name):
        if not self.exist_dir(dir_name):
            return None
        file_list = []
        path_list = os.listdir(dir_name)
        for path in path_list:
            tmp_dir = os.path.join(dir_name, path)
            if os.path.isdir(tmp_dir) and not path.endswith("/"):
                path = path + "/"
            file_list.append(path)
        return '\n'.join(file_list)
    
    def list_dir(self, dir_name):
        if not self.exist_dir(dir_name):
            return None
        file_list = []
        path_list = os.listdir(dir_name)
        for path in path_list:
            tmp_dir = os.path.join(dir_name, path)
            if os.path.isdir(tmp_dir):
                file_list.append(tmp_dir)

        return file_list
    
    def list_files(self, dir_name):
        if not self.exist_dir(dir_name):
            return None
        file_list = []
        path_list = os.listdir(dir_name)
        for path in path_list:
            tmp_dir = os.path.join(dir_name, path)
            if not os.path.isdir(tmp_dir):
                file_list.append(tmp_dir)

        return file_list

    # 获取脚本文件的当前路径
    def cur_file_dir(self):
        path = sys.path[0]
        if os.path.isdir(path):
            return path
        elif os.path.isfile(path):
            return os.path.dirname(path)
        return None

    def file_tail(self, file_path):
        cmd = "tail %s" % file_path
        stdout, stderr, retcode = no_block_sys_cmd.NoBlockSysCommand(
                ).run_once(cmd)
        if retcode != 0:
            return None
        return stdout

if __name__ == '__main__':
    linux_cmd = LinuxFileCommand()
    print(linux_cmd.file_tail('./linux_file_cmd.py'))

    