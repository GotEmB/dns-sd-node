dns_sd = require "./build/Release/dns_sd"
events = require "events"
net = require "net"

exports.advertise = ->
	dns_sd.DNSServiceAdvertisement()

exports.browse = ->
	browser = dns_sd.DNSServiceAdvertisement arguments...
	browser.__proto__ = events.EventEmitter::
	sock = new net.Socket fd: browser.sockFd
	processResult = browser.processResult
	terminate = browser.terminate
	browser.removeInits()
	do loop = ->
		sock.once "data", ->
			processResult()
	# @on ""
	browser.terminate = ->
		sock.destroy()
		terminate()