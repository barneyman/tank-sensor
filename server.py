from flask import Flask, request, send_from_directory
from concurrent.futures import ThreadPoolExecutor
import json

from bjfGoogle import bjfGoogle, bjfFusionService, bjfSheetsService
#import bjfGoogle

from datetime import datetime
from datetime import timedelta

app=Flask(__name__)

@app.route("/")
def main():
    return "Welcome!"

defaultDirectory="/home/pi/tank/"

#upgradesDict={ 'tank_1.0':'TankSensor_1_1.bin','tank_1.1':'TankSensor_1_2.bin','tank_1.2':'TankSensor_1_3.bin' }
FlaskPort=5000

@app.route('/upgrades/<path:filename>')
def send_js(filename):
	print("sending "+filename)
	return send_from_directory(defaultDirectory+'upgrades', filename)

@app.route("/data", methods=['GET', 'POST'])
def data():
	if request.method == 'GET':
		print( "data GET" )
		return "data"
	else:
		if not request.json:
			print ("error")
			print (request.data)
			return "error"

		executor.submit(ProcessJSON, request.json)

		try:
			# create a json blob to send back
			timenow=datetime.now()
			returnBlob = {'localTime': timenow.strftime("%H:%M:%S"), 'reset': False}
			# get the version running
			clientVer=request.json['version']

			upgradesDict={}

			try:
				upgradesDict=json.load(open(defaultDirectory+"upgrades/available.json"))
			except Exception as e:
				print ("parsing problem ",e)

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

def ProcessJSON(jsonData):
	#print (jsonData)
	#print("=======")
	#print (jsonData["data"])
	# work out how many times we have to do this
	try:

		# the [:] means take a copy of thing to  iterate with - i like python
		archiveRowCount=0
		print("archived items ",len(failedRowValues));
		for row in failedRowValues[:]:
			# don't try tpoo many times and swamp poor google
			archiveRowCount=archiveRowCount+1
			if archiveRowCount>5:
				break
			try:
				print("retrying row ",row)
				fusion.InsertRowData(tpTable,row)
				# workled, remove the the ORIGINAL list
				failedRowValues.remove(row)
			except Exception as e:
				print("fusion retry failed ",e)
			try:
				sheetsService.AppendSheetRange('1hVkEaao2yQ6g680cfmSKf6PiZUFidZwlI_8EWsFN7s0', row, 'HISTORY_DATA')
			except Exception as e:
				print("sheets retry failed - don't care",e)


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

			lastRowValues=[utcForIteration.strftime("%Y-%m-%d %H:%M:%S"),localForIteration.strftime("%Y-%m-%d %H:%M:%S"), reading["iter"], reading["tempC"],reading["pressMB"],reading["humid%"], reading["distCM"] ]

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
				# fusion is the important one
				try:
					fusion.InsertRowData(tpTable,rowValues)
				except Exception as e:
					print("fusion exception ",e)
					if len(failedRowValues) < 1000:
						failedRowValues.append(rowValues)
						print ("queued row ")
				try:
					sheetsService.AppendSheetRange('1hVkEaao2yQ6g680cfmSKf6PiZUFidZwlI_8EWsFN7s0', rowValues, 'HISTORY_DATA')
				except Exception as e:
					print("sheets exception ",e)


		#  deep copy
		latestRowData = [i for i in lastRowValues]
		rowValues=[latestRowData]
		latestRowData.append(utcNow.strftime("%Y-%m-%d %H:%M:%S"))
		sheetsService.UpdateSheetRange('1hVkEaao2yQ6g680cfmSKf6PiZUFidZwlI_8EWsFN7s0', rowValues,'LATEST_DATA')
		print( "processed")
	except Exception as e:
		print ("Async Exception occurred ",e)


if __name__ == "__main__":
	thisG=bjfGoogle()
	thisG.Authenticate(defaultDirectory+"config/client_secret.json",defaultDirectory+"config/credentialStore.credentials", "https://www.googleapis.com/auth/spreadsheets https://www.googleapis.com/auth/fusiontables")
	# create a fusion table
	fusion=bjfFusionService(thisG)
	sheetsService=bjfSheetsService(thisG)
	# seeif our table is there
	tableName="tempPressure"
	tpTable=fusion.GetTableByName(tableName)

	if tpTable==None:
		print("Creating fusion Table ...")
		tableDef={"name":tableName,"isExportable":False, "columns":[{"name":"whenUTC","type":"DATETIME"},{"name":"local","type":"DATETIME"},{"name":"iter","type":"NUMBER"},{"name":"tempC","type":"NUMBER"},{"name":"pressureMB","type":"NUMBER"},{"name":"humidity%","type":"NUMBER"},{"name":"distanceCM","type":"NUMBER"}]  }
		tpTable=fusion.CreateTable(tableDef)

	if tpTable!=None:
		print("running")
		executor = ThreadPoolExecutor(2)
		app.run(host="0.0.0.0", port=FlaskPort)









