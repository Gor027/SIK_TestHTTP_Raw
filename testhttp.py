import sys
import subprocess
import os
import signal

try:
    from urllib.parse import urlparse
except ImportError:
     from urlparse import urlparse

if len(sys.argv) != 3:
    print("Usage: testhttp cookiesFile testedPage")
    sys.exit()
    
fileName = sys.argv[1]
testedPage = sys.argv[2]

def callRaw(parsed):
	host = parsed.hostname
	port = parsed.port

	if port is None:
		port = 80

	con = host + ":" + str(port)
	args = ['./testhttp_raw', con, fileName, testedPage]

	subprocess.call(args)

    
def callStunnel(parsed):
	direct = os.getcwd()

	host = parsed.hostname
	port = parsed.port

	if port is None:
		port = 443

	con = host + ":" + str(port)

	f = open("stunnel.conf", "w+")
	f.write("pid = " + direct + "/pid.key" + "\r\n")
	f.write("[service]\r\nclient = yes\r\naccept = 127.0.0.1:11011\r\n")
	f.write("connect = " + con + "\r\n")
	
	f.close()

	os.system("stunnel stunnel.conf")

	callArgs = "./testhttp_raw 127.0.0.1:11011 " + fileName + " " + testedPage
	os.system(callArgs)
	
	# Kill the process here
	fp = open("pid.key", "r")
	pid = fp.read()
	fp.close()

	os.system("kill " + pid)
	os.remove("stunnel.conf")
	# os.remove("pid.key")


parsed = urlparse(testedPage)

if parsed.scheme == "http":
    callRaw(parsed)
else:
    callStunnel(parsed)

