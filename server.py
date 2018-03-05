from flask import Flask, request, send_from_directory
from concurrent.futures import ThreadPoolExecutor
import json
import signal

from bjfGoogle import bjfGoogle, bjfFusionService, bjfSheetsService
#import bjfGoogle

from datetime import datetime
from datetime import timedelta

app=Flask(__name__)

@app.route("/")
def main():
    return "Welcome!"

#pi
defaultDirectory="/home/pi/tank/"
# windows
#defaultDirectory="c:\\scribble\\"

#upgradesDict={ 'tank_1.0':'TankSensor_1_1.bin','tank_1.1':'TankSensor_1_2.bin','tank_1.2':'TankSensor_1_3.bin' }
FlaskPort=5000


@app.route('/upgrades/<path:filename>')
def send_js(filename):
	print("sending "+filename)
	return send_from_directory(defaultDirectory+'upgrades', filename)



@app.route("/data", methods=['GET', 'POST'])
def data():
	if request.method == 'GET':

		lastSeenValues=[]
		print( "data GET" )
		print (lastSeenValues)
		try:
			lastSeenValues=json.load( open(defaultDirectory+"config/lastSeenData.json"))
		except:
			print ("no last data")

		return json.dumps(lastSeenValues, separators=(',', ':'));
	else:
		if not request.json:
			print ("error")
			print (request.data)
			return "error"

		executor.submit(ProcessJSON, request.json)

		try:
			# create a json blob to send back
			timenow=datetime.now()
			returnBlob = {'localTime': timenow.strftime("%H:%M:%S"), 'minutes':timenow.strftime("%M"),'seconds':timenow.strftime("%S"), 'reset': False}
			# get the version running
			clientVer=request.json['version']

			upgradesDict={}

			try:
				upgradesDict=json.load(open(defaultDirectory+"upgrades/available.json"))
			except Exception as e:
				print ("parsing problem ",e)

			# expects { "latest":"tank_1.11", "binary":"TankSensor_1.11.bin"}
			if not "latest" in upgradesDict or not "binary" in upgradesDict:
				latestVer=clientVer
			else:
				latestVer=upgradesDict['latest']


			if clientVer != latestVer:
				#upgradeDict={'host':{{ request.host.split(':')[0] }}, 'port':FlaskPort, 'url':upgradesDict[clientVer]}
				#upgradeDict={'host':'192.168.43.22', 'port':FlaskPort, 'url':'upgrades/'+upgradesDict[clientVer]}
				print("upgrade potential! ", clientVer,latestVer)

				upgradeDict={'url':'upgrades/'+upgradesDict["binary"]}
				returnBlob['upgrade']=upgradeDict

			return json.dumps(returnBlob, separators=(',', ':'));

		except Exception as e:
			print ("Exception occurred ",e)
			return "bad";

failedRowValues=[]

def sigterm_handler(_signo, _stack_frame):
    # Raises SystemExit(0):
	
	writeStaleData()
	sys.exit(0)


def writeStaleData():	
	if len(failedRowValues)>0:
		print("scribbling the stales to file")
		json.dump(failedRowValues, open(defaultDirectory+"config/staleData.json",'w'))

def PublishJSON(maxRows):	
	# the [:] means take a copy of thing to  iterate with - i like python
	archiveRowCount=0
	print("archived items ",len(failedRowValues));
	# the [:] means take a copy of thing to  iterate with - i like python
	
	for row in failedRowValues[:]:
		# don't try tpoo many times and swamp poor google
		archiveRowCount=archiveRowCount+1
		bailNow=False
		if archiveRowCount>maxRows:
			break
		try:
			print("retrying row ",row)
			#raise ValueError('faking a fusion error - collect a few of these.')
			theTable=getOrCreateTable(row['table'])		
			fusion.InsertRowData(theTable,row['data'])
			# workled, remove the the ORIGINAL list
			failedRowValues.remove(row)
		except Exception as e:
			print("fusion retry failed ",e)
			# we need to bail on fail or we risk breaking sequence
			bailNow=true
		try:
			if bailNow==False:
				sheetsService.AppendSheetRange('1hVkEaao2yQ6g680cfmSKf6PiZUFidZwlI_8EWsFN7s0', row, 'HISTORY_DATA')
		except Exception as e:
			print("sheets retry failed - don't care",e)
			
		if bailNow == True:
			break
	
	
def ProcessJSON(jsonData):
	#print (jsonData)
	#print("=======")
	#print (jsonData["data"])
	#print("=======")
	# work out how many times we have to do this
	try:

		# push any stale values
		PublishJSON(2)

		rowValues=[]

		utcNow=datetime.utcnow()
		localNow=datetime.now()
		# 'current' iteration is 'now' so subtract
		currentIter=jsonData["current"]
		intervalSeconds=jsonData["intervalS"]
		#utcStart=utcNow-timedelta(seconds=jsonData["intervalS"]*(len(jsonData["data"]))-1)
		#localStart=datetime.now()-timedelta(seconds=jsonData["intervalS"]*(len(jsonData["data"]))-1)
		for reading in jsonData["data"]:
			#print (reading)
			utcForIteration=utcNow-(timedelta(seconds=intervalSeconds*(currentIter-reading["iter"])))
			localForIteration=localNow-(timedelta(seconds=intervalSeconds*(currentIter-reading["iter"])))

			lastRowValues=[utcForIteration.strftime("%Y-%m-%d %H:%M:%S"),localForIteration.strftime("%Y-%m-%d %H:%M:%S"), reading["iter"], reading["tempC"],reading["pressMB"],reading["humid%"], reading["distCM"], reading["lux"], reading["lipo"] ]

			# DEBUG - seive to minutes only
			#if reading["iter"] % 6 == 0:
			rowValues.append(lastRowValues)


		# add to history
		# DEBUG
		if len(rowValues)>0 :

			# to keep sequential order, if we have anything stale, add to the back of it
			if len(failedRowValues) > 0:
				failedRowValues.append(rowValues)
				print ("force queued row")
			else:
				try:
					# fusion is the important one
					tableName=jsonData["host"]+"_tempPressure";
					theTable=getOrCreateTable(tableName)		
					#raise ValueError('faking a fusion error - collect a few of these.')
					fusion.InsertRowData(theTable,rowValues)
					try:
						sheetsService.AppendSheetRange('1hVkEaao2yQ6g680cfmSKf6PiZUFidZwlI_8EWsFN7s0', rowValues, 'HISTORY_DATA')
					except Exception as e:
						print("sheets exception ",e)
				except Exception as e:
					print("fusion exception ",e)
					if len(failedRowValues) < 1000:
						failedRowValues.append({ "table":tableName,"data":rowValues})
						print ("queued row ")


		# deepcopy
		json.dump(lastRowValues, open(defaultDirectory+"config/lastSeenData.json",'w'))
		#  deep copy
		latestRowData = [i for i in lastRowValues]
		rowValues=[latestRowData]
		latestRowData.append(utcNow.strftime("%Y-%m-%d %H:%M:%S"))
		sheetsService.UpdateSheetRange('1hVkEaao2yQ6g680cfmSKf6PiZUFidZwlI_8EWsFN7s0', rowValues,'LATEST_DATA')
		print( "processed")
	except Exception as e:
		print ("Async Exception occurred ",e)
		

		
def getOrCreateTable(tableName):
	theTable=fusion.GetTableByName(tableName)
	if theTable==None:
		print("Creating fusion Table ...", tableName)
		tableDef={"name":tableName,"isExportable":False, "columns":[{"name":"whenUTC","type":"DATETIME"},{"name":"local","type":"DATETIME"},{"name":"iter","type":"NUMBER"},{"name":"tempC","type":"NUMBER"},{"name":"pressureMB","type":"NUMBER"},{"name":"humidity%","type":"NUMBER"},{"name":"distanceCM","type":"NUMBER"},{"name":"lux","type":"NUMBER"},{"name":"lipoV","type":"NUMBER"}]  }
		theTable=fusion.CreateTable(tableDef)
	return theTable


if __name__ == "__main__":
	thisG=bjfGoogle()
	thisG.Authenticate(defaultDirectory+"config/client_secret.json",defaultDirectory+"config/credentialStore.credentials", "https://www.googleapis.com/auth/spreadsheets https://www.googleapis.com/auth/fusiontables")
	# create a fusion table
	fusion=bjfFusionService(thisG)
	sheetsService=bjfSheetsService(thisG)
	# see if our table is there

	#tableName="tempPressure"
	#tpTable=fusion.GetTableByName(tableName)
	
	#signal.signal(signal.SIGTERM, sigterm_handler)
	#signal.signal(signal.SIGINT, sigterm_handler)
	
	print ("reading stale data "),
	try:
		failedRowValues = json.load(open(defaultDirectory+"config/staleData.json"))
		print(len(failedRowValues))
		PublishJSON(20)
	except:
		failedRowValues=[]
	
	if True: #tpTable!=None:
		print("running")
		executor = ThreadPoolExecutor(2)
		app.run(host="0.0.0.0", port=FlaskPort)

		writeStaleData()







