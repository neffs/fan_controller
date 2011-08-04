#!/usr/bin/env python

import usb
import sys
import time

vendor_id = 0x16c0
product_id = 0x05df

class FanController(object) :
    def __init__(self, device):
        self.device = device
        self.handle = device.open()
        #self.handle.claimInterface(0)
        
    def getSpeed(self):
        speed = self.handle.controlMsg(usb.TYPE_VENDOR | usb.RECIP_DEVICE | usb.ENDPOINT_IN, 0x2,  4, 0x0, 0, 5000)
        return list(speed)
        
    def getTacho(self):
        speed = self.handle.controlMsg(usb.TYPE_VENDOR | usb.RECIP_DEVICE | usb.ENDPOINT_IN, 0x3,  4, 0x0, 0, 5000)
        return [a*27 for a in speed]
        
    def setSpeed(self, fan1, fan2, fan3, fan4):
        buffer = (fan1, fan2, fan3, fan4)
        self.handle.controlMsg(usb.TYPE_VENDOR | usb.RECIP_DEVICE | usb.ENDPOINT_OUT, 0x1,  buffer, 0x0, 0, 5000)        
        
        
def getDevice():
    devices = {}
    buses = usb.busses()
    for bus in buses :
       for device in bus.devices :
            if device.idVendor == vendor_id :
                if device.idProduct == product_id :
                    d = FanController(device)
                    
    return d
    

def main(argv=None) :
    dev = getDevice()
    speed = int(argv[1])
    dev.setSpeed(speed, speed, speed, speed)
    print dev.getSpeed()
    print dev.getTacho()
    
if __name__ == "__main__":
    main(sys.argv)