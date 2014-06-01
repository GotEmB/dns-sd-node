{
	"conditions": [
        [ 'OS=="mac"', {
			"make_global_settings": [
				["CXX","/usr/bin/clang++"],
				["LINK","/usr/bin/clang++"]
			]
		}]
	],
	"targets": [
		{
			"target_name": "dns_sd",
			"sources": [
				"src/dns_sd.cc"
			], 
			"cflags": [ "-std=c++11", "-stdlib=libc++" ],
			"xcode_settings": {
				"OTHER_CFLAGS": [ "-std=c++11", "-stdlib=libc++", "-mmacosx-version-min=10.7" ]
			}
		}
	]
}