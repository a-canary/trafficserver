#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

#JAVAPLATF-1900, we need this to detect rhel7
import platform
import os
import tempfile
import subprocess

import tsqa.environment
import tsqa.test_cases
import tsqa.utils

unittest = tsqa.utils.import_unittest()

# TODO: check that the given path is relative
def tests_file_path(path):
    '''
    Return the absolute path to a file with relative path "name" from tsqa/files
    '''
    base = os.path.realpath(os.path.join(__file__, '..', '..', 'files'))
    return os.path.join(base, path)

class EnvironmentCase(tsqa.test_cases.EnvironmentCase):
    '''
    This class will get an environment (which is unique) but won't start it
    '''
    @classmethod
    def getEnv(cls):
        '''
        This function is responsible for returning an environment
        '''
        SOURCE_DIR = os.path.realpath(os.path.join(__file__, '..', '..', '..', '..'))
        TMP_DIR = os.path.join(tempfile.gettempdir(), 'tsqa')
        ef = tsqa.environment.EnvironmentFactory(SOURCE_DIR,
                                                 os.path.join(TMP_DIR, 'base_envs'),
                                                 default_configure={'enable-example-plugins': None,
                                                                    'enable-test-tools': None,
                                                                    'disable-dependency-tracking': None,
                                                                    },
                                                 )
        # TODO: figure out a way to determine why the build didn't fail and
        # not skip all build failures?
        try:
            return ef.get_environment(cls.environment_factory.get('configure'), cls.environment_factory.get('env'))
        except Exception as e:
            raise unittest.SkipTest(e)
    
    @classmethod
    def setUpEnv(cls, env):
        # This is unique to Yahoo! to get TSQA working w/ Yahoo Connection
        cls.configs['records.config']['CONFIG']['proxy.config.proxy_name'] = 'localhost'
        # JAVAPLATF-1900, rhel7 moves things
        if (platform.platform().find("redhat-7")==-1):
            proc = subprocess.Popen(["cp", "/home/y/libexec64/trafficserver/yahoo_connection.so",
              "{0}/yahoo_connection.so".format(cls.environment.layout.plugindir)], stdout=subprocess.PIPE,
              stderr=subprocess.PIPE)
            subprocess.Popen(["chmod", "755", "{0}/yahoo_connection.so".format(cls.environment.layout.plugindir)])
        else:
            # oops. root was screwed up.
            proc = subprocess.Popen(["cp", "/usr/local/libexec64/trafficserver/yahoo_connection.so",
              "{0}/yahoo_connection.so".format(cls.environment.layout.plugindir)], stdout=subprocess.PIPE,
              stderr=subprocess.PIPE)
            subprocess.Popen(["chmod", "755", "{0}/yahoo_connection.so".format(cls.environment.layout.plugindir)])