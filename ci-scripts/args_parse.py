# SPDX-License-Identifier: LicenseRef-CSSL-1.0

#---------------------------------------------------------------------
# Python for CI testing
#
#   Required Python Version
#     Python 3.x
#
#   Required Python Package
#     pexpect
#---------------------------------------------------------------------

#-----------------------------------------------------------
# Import Libs
#-----------------------------------------------------------
import sys		# arg
import re		# reg
import constants as CONST

#-----------------------------------------------------------
# Parsing Command Line Arguements
#-----------------------------------------------------------


def ArgsParse(argvs,CiTestObj,RAN,HTML,CONTAINERS,HELP,CLUSTER):

    force_local = False
    date_fmt = None
    while len(argvs) > 1:
        myArgv = argvs.pop(1)	# 0th is this file's name

	    #--help
        if re.match(r'^\-\-help$', myArgv, re.IGNORECASE):
            HELP.GenericHelp(CONST.Version)
            sys.exit(0)
        if re.match(r'^\-\-local$', myArgv, re.IGNORECASE):
            force_local = True


	    #consider inline parameters
        elif re.match(r'^\-\-datefmt=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-datefmt=(.+)$', myArgv, re.IGNORECASE)
            date_fmt = matchReg.group(1)
        elif re.match(r'^\-\-mode=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-mode=(.+)$', myArgv, re.IGNORECASE)
            mode = matchReg.group(1)
        elif re.match(r'^\-\-repository=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-repository=(.+)$', myArgv, re.IGNORECASE)
            CiTestObj.repository = matchReg.group(1)
            RAN.repository=matchReg.group(1)
            HTML.repository=matchReg.group(1)
            CONTAINERS.repository=matchReg.group(1)
            CLUSTER.repository=matchReg.group(1)
        elif re.match(r'^\-\-ranAllowMerge=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-ranAllowMerge=(.+)$', myArgv, re.IGNORECASE)
            doMerge = matchReg.group(1)
            if ((doMerge == 'true') or (doMerge == 'True')):
                RAN.merge=True
                CONTAINERS.merge=True
                CLUSTER.merge=True
        elif re.match(r'^\-\-branch=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-branch=(.+)$', myArgv, re.IGNORECASE)
            CiTestObj.branch = matchReg.group(1)
            RAN.branch=matchReg.group(1)
            HTML.branch=matchReg.group(1)
            CONTAINERS.branch=matchReg.group(1)
            CLUSTER.branch=matchReg.group(1)
        elif re.match(r'^\-\-targetBranch=(.*)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-targetBranch=(.*)$', myArgv, re.IGNORECASE)
            RAN.targetBranch=matchReg.group(1)
            CONTAINERS.targetBranch=matchReg.group(1)
            CLUSTER.targetBranch=matchReg.group(1)
        elif re.match(r'^\-\-workspace=(.+)$|^\-\-eNBSourceCodePath=(.+)$', myArgv, re.IGNORECASE):
            if re.match(r'^\-\-workspace=(.+)$', myArgv, re.IGNORECASE):
                matchReg = re.match(r'^\-\-workspace=(.+)$', myArgv, re.IGNORECASE)
            else:
                matchReg = re.match(r'^\-\-eNBSourceCodePath=(.+)$', myArgv, re.IGNORECASE)
            RAN.workspace=matchReg.group(1)
            CONTAINERS.workspace=matchReg.group(1)
            CLUSTER.workspace=matchReg.group(1)
        elif re.match(r'^\-\-XMLTestFile=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-XMLTestFile=(.+)$', myArgv, re.IGNORECASE)
            CiTestObj.testXMLfiles.append(matchReg.group(1))
            HTML.testXMLfiles.append(matchReg.group(1))
            HTML.nbTestXMLfiles=HTML.nbTestXMLfiles+1
        elif re.match(r'^\-\-finalStatus=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-finalStatus=(.+)$', myArgv, re.IGNORECASE)
            finalStatus = matchReg.group(1)
            if ((finalStatus == 'true') or (finalStatus == 'True')):
                CiTestObj.finalStatus = True
        elif re.match(r'^\-\-OCUserName=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-OCUserName=(.+)$', myArgv, re.IGNORECASE)
            CLUSTER.OCUserName = matchReg.group(1)
        elif re.match(r'^\-\-OCPassword=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-OCPassword=(.+)$', myArgv, re.IGNORECASE)
            CLUSTER.OCPassword = matchReg.group(1)
        elif re.match(r'^\-\-OCProjectName=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-OCProjectName=(.+)$', myArgv, re.IGNORECASE)
            CLUSTER.OCProjectName = matchReg.group(1)
        elif re.match(r'^\-\-OCUrl=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-OCUrl=(.+)$', myArgv, re.IGNORECASE)
            CLUSTER.OCUrl = matchReg.group(1)
        elif re.match(r'^\-\-OCRegistry=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-OCRegistry=(.+)$', myArgv, re.IGNORECASE)
            CLUSTER.OCRegistry = matchReg.group(1)
        elif re.match(r'^\-\-FlexRicTag=(.+)$', myArgv, re.IGNORECASE):
            matchReg = re.match(r'^\-\-FlexRicTag=(.+)$', myArgv, re.IGNORECASE)
            CONTAINERS.flexricTag = matchReg.group(1)
        else:
            HELP.GenericHelp(CONST.Version)
            sys.exit('Invalid Parameter: ' + myArgv)

    return mode, force_local, date_fmt
