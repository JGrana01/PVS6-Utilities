#
# define paths and info here
#

# anchor everyting in the present dirctory

# local directories and apps

ANCHOR="/home/admin/pvs6/port"

APPSDIR="$ANCHOR"
DATADIR="$ANCHOR/data"
REPORTSDIR="$ANCHOR/reports"
MAILER="/home/admin/mailer/sndmail"
MAILTXT="/tmp/mailtxt"

# rPi proxy to Sunpower PVS6

pvslocal="192.168.1.63"		# sometimes online - sometimes now (not used presently)
#piProxy="192.168.1.223"		# this is the IP address of the Raspberry Pi Proxy.
piProxy="localhost"		# this is the IP address of the Raspberry Pi Proxy.
sunpowerUrl="http://$piProxy/cgi-bin/dl_cgi?Command=DeviceList"

# Set InfluxDB connection details

INFLUX_URL="http://localhost:8086"
# INFLUX_TOKEN="U6EtEnSz-bwE09BLlrI2SXft5Y8OFOhenJ34oK7LVzADkv660Algl1L6Ri2Yrg4W-T023-qH_Gx1bM7YWVZPjA=="
INFLUX_TOKEN="A4Eb28SY4-eCdAccyg-31xaDtv3s-mKHxXuMYyvg3jvcN1baMiHH7izGDrUxk37gtPAgYGxKT2UJFI7zVjqy0Q=="
INFLUX_ORG="GranaHome"
#INFLUX_ORG="Home"
INFLUX_BUCKET="SolarTest"

# local flags for logging/csv file
#

ON=1
OFF=0
NOLOG=0
INFO=1
WARN=2
ERROR=3
BOTH=4

PANELSCSV=$ON	# create a seperate panels csv daily
STARTHOUR=11	# Best time of day to run checkpanels
ENDHOUR=13	# for maximum sunlight on the panels

# local files
#

PVSRAW="$DATADIR/pvsout.json"
PVSRAW2="$DATADIR/pvsout2.json"
PVSDATA="$DATADIR/pvsdata"


#
# local variables - logging and email

CHKRETRY=3	# number of retries to see if proxy and pvs are on the network
debug=$OFF
loglevel="$NOLOG"
sndemail=$ON

# misc
today=$(date '+%m-%d-%Y %H:%M')
dateis=$(date '+%m-%d-%y')
AUTOLIGHT=$ON	# set for checking if time is beyween dawn and dusk
		# if outside light is too low, dont bother.
		# set to OFF if you want to run cron all the time
KEEPDBFILLED=$ON	# write the last known measurements before dusk

#longitude="-122.4386" # if using AUTOLIGHT - set to your GPS coordinates
#latitude="40.5538"
longitude="-122.4347"
latitude="40.5538"
timezone="America/Los_Angeles"
zipcode="96001"
#timezone="PST"

SOLARAPIKEY="vmsVGmbbQoHPyLqt-QceOVvHNa0wCjI4"

#
# some misc functions used by pvs6 and others
#


#
# dlog - output strings based on debug level and log/loglevel
#
# debug=OFF and loglevel NOLOG - always silent
# debug=ON - print everthing
# loglevel = output info, warnings, errors (info print everthing, warn prints just warning and error, error - just errors
#

dlog() {

	if [ "$debug" -eq "$ON" ]; then
		printf "%s DEBUG: %s\\n" "$today" "$2"
	fi

	if [ "$loglevel" -eq "$NOLOG" ]; then
		return 1
	fi

	case $1 in
		"$INFO")
			if [ "$loglevel" -le "$INFO" ]; then
				printf "%s INFO: %s\\n" "$today" "$2" >> $DATADIR/logfile
			fi
			;;
		"$WARN")
			if [ "$loglevel" -le "$WARN" ]; then
				printf "%s WARN: %s\\n" "$today" "$2" >> $DATADIR/logfile
			fi
			;;
		"$ERROR")
			if [ "$loglevel" -le "$ERROR" ]; then
				printf "%s ERROR: %s\\n" "$today" "$2" >> $DATADIR/logfile
				logger -t "pvs6" "$2"
			fi
			;;
		*)
			;;
	esac
}


#
# rprint - output to STDOUT or store report information in a report file
#
#

rprint() {

	case $1 in
		"$ON")
			printf "$2" >> $REPORT
			;;
		"$OFF")
			printf "$2" 
			;;
		"$BOTH")
			printf "$2" >> $REPORT
			printf "$2" 
			;;
		*)
			;;
	esac
}

#
# pseudo breakpoint for shell debugging
#

break() {

	printf "\\n--Breakpoint $1-- ?"
	read bpi
	if [ $bpi = "q" ] || [ $bpi = "Q" ]; then
		echo
		exit
	fi
}



#
# getpvsdata - retrieve the json file from pvs6 using the command Device List
#


getpvsdata() {

	cat /dev/null > $1

	curl -s -o $1 -X GET --header 'Accept: application/json' $sunpowerUrl

	outsize=$(cat $1 | wc -l)

	if [ $outsize -lt 10 ]; then
		logger -t "pvs6" "Data from PVS6 too small"
		printf "\\n %s:  Data from PVS6 too small size was %s\\n\\n" "$today" $outsize > $MAILTXT
		$MAILER "Data from PVS6 too small"
		printf -v dstring "Data from PVS6 too small size was %s" $outsize
		dlog $ERROR "$dstring"
		exit
	fi
}

#
# checkproxy - make sure the rPi proxy is online
#
#	exit since if we can't talk to proxy, no use in continuing
#

checkproxy() {

#
# prime the pump
#

	ping -q -c 5 $piProxy > /dev/null

	if ! ping -q -c 2 -W 2 $piProxy >/dev/null; then
		logger -t "$SCRIPTNAME" "rPi Proxy not responding"
		printf "\\n rPi Proxy %s not not responding on %s\\n\\n" $piProxy "$today"  > $MAILTXT
		$MAILER "rPi Proxy Unresponsive"
		echo "Proxy in error" > $DATADIR/inerror
		printf -v dstring "rPi Proxy %s not not responding" $piProxy 
		dlog $ERROR "$dstring"
		exit
	fi
}

#
# checkpvs - send an http command to PVS6 (via proxy) and expect a 403 Forbidden.
#

checkpvs() {

    # Send a HEAD request and get the HTTP status code
    local status_code=$(curl -o /dev/null -s -w "%{http_code}" "$piProxy")

    if [ "$status_code" -ne 403 ]; then
	logger -t "$SCRIPTNAME" "PVS6 not responding to http request"
        printf -v dstring "PVS6 at IP %s not responding to request.\\nPVS6 is offline or proxy is not forwarding\\n" $piProxy
	dlog $ERROR "$dstring"
        return 1
    else
	return 0
    fi
}

convert_seconds() {

    local days=$(($1 / 86400))
    local hours=$(( ($1 % 86400) / 3600 ))
    local minutes=$(( ($1 % 3600) / 60 ))

    echo "${days}d ${hours}h ${minutes}m"
}

