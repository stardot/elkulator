#!/usr/bin/env python

"""
extract_frames.py - Extract frames from VID files as PNG files for further
                    processing.

Copyright (C) 2016 David Boddie <david@boddie.org.uk>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
"""

import os, stat, sys
import Image

if __name__ == "__main__":

    if len(sys.argv) != 3:
    
        sys.stderr.write("Usage: %s <movie data file> <output directory>\n" % sys.argv[0])
        sys.exit(1)
    
    output_dir = sys.argv[2]
    if not os.path.exists(output_dir):
        os.mkdir(output_dir)
    
    size = os.stat(sys.argv[1])[stat.ST_SIZE]
    frames = size/(640 * 256)
    field_size = len(str(frames))
    template = "%%0%ii.png" % field_size
    
    f = open(sys.argv[1], "rb")
    frame = 0
    
    while True:
    
        data = f.read(640 * 256)
        if not data or len(data) < 640 * 256:
            break
        
        im = Image.fromstring("P", (640, 256), data)
        im.putpalette("\x00\x00\x00"
                      "\xff\x00\x00"
                      "\x00\xff\x00"
                      "\xff\xff\x00"
                      "\x00\x00\xff"
                      "\xff\x00\xff"
                      "\x00\xff\xff"
                      "\xff\xff\xff")
        im = im.resize((640, 512), Image.NEAREST)
        im.save(os.path.join(output_dir, template % frame))
        frame += 1
    
    sys.exit()
