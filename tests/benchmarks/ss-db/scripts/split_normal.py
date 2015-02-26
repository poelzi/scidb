#!/usr/bin/python
# BEGIN_COPYRIGHT
#
# This file is part of SciDB.
# Copyright (C) 2008-2011 SciDB, Inc.
#
# SciDB is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# SciDB is distributed "AS-IS" AND WITHOUT ANY WARRANTY OF ANY KIND,
# INCLUDING ANY IMPLIED WARRANTY OF MERCHANTABILITY,
# NON-INFRINGEMENT, OR FITNESS FOR A PARTICULAR PURPOSE. See
# the GNU General Public License for the complete license terms.
#
# You should have received a copy of the GNU General Public License
# along with SciDB.  If not, see <http://www.gnu.org/licenses/>.
# END_COPYRIGHT
#
import csv
import time
import os
import subprocess
import sys
import traceback
sys.path.append(os.getcwd()) # NOCHECKIN 
sys.path.append('/opt/scidb/11.12/lib')
import scidbapi as scidb


# Start the single node server. 

def handleException(inst, exitWhenDone, op=None):
    traceback.print_exc()
    if op:
        print >> sys.stderr, "Exception while ", op
    print >> sys.stderr, "     Exception Type: %s" % type(inst)     # the exception instance
    print >> sys.stderr, "     Exception Value: %r" % inst 
    print >> sys.stderr, ""
    if(exitWhenDone):
        exit(2)

def main():
    size=7500
    db = scidb.connect("localhost", 1239)
    pos=[(490205,507962,0),(495230,488864,1),(661084,399169,2),(505281,498915,3),(486183,503940,4),(491208,484842,5),(496234,489868,6),(702370,440455,7),(506284,499918,8),(487186,504943,9),(492212,485846,10),(536898,274984,11),(502262,495896,12),(507288,500922,13),(488190,505947,14),(493215,486849,15),(578184,316270,16),(503266,496900,17),(508291,501925,18),(489193,506950,19),(494219,487853,20),(619470,357556,21),(504269,497903,22),(485171,502929,23),(490197,507954,24),(495222,488856,25),(660756,398842,26),(505273,498907,27),(486175,503932,28),(491200,484834,29),(496226,489860,30),(702042,440127,31),(506276,499910,32),(487178,504936,33),(492204,485838,34),(536570,274656,35),(502254,495888,36),(507280,500914,37),(488182,505939,38),(493207,486841,39),(577856,315942,40),(503258,496892,41),(508283,501917,42),(489185,506942,43),(494211,487845,44),(619142,357228,45),(504261,497895,46),(485164,502921,47),(490189,507946,48),(495214,488848,49),(660428,398514,50),(505265,498899,51),(486167,503924,52),(491192,484826,53),(496218,489852,54),(701714,439800,55),(506268,499902,56),(487170,504928,57),(492196,485830,58),(536243,274328,59),(502246,495880,60),(507272,500906,61),(488174,505931,62),(493199,486833,63),(577528,315614,64),(503250,496884,65),(508275,501909,66),(489177,506934,67),(494203,487837,68),(618814,356900,69),(504253,497887,70),(485156,502913,71),(490181,507938,72),(495206,488840,73),(660100,398186,74),(505257,498891,75),(486159,503916,76),(491184,484818,77),(496210,489844,78),(701386,439472,79),(506260,499894,80),(487162,504920,81),(492188,485822,82),(535915,274000,83),(502238,495872,84),(507264,500898,85),(488166,505923,86),(493191,486825,87),(577201,315286,88),(503242,496876,89),(508267,501901,90),(489169,506927,91),(494195,487829,92),(618486,356572,93),(504245,497879,94),(485148,502905,95),(490173,507930,96),(495198,488832,97),(659772,397858,98),(505249,498883,99),(486151,503908,100),(491176,484810,101),(496202,489836,102),(701058,439144,103),(506252,499886,104),(487155,504912,105),(492180,485814,106),(535587,273673,107),(502230,495864,108),(507256,500890,109),(488158,505915,110),(493183,486817,111),(576873,314958,112),(503234,496868,113),(508259,501893,114),(489161,506919,115),(494187,487821,116),(618159,356244,117),(504237,497871,118),(485140,502897,119),(490165,507922,120),(495190,488824,121),(659444,397530,122),(505241,498875,123),(486143,503900,124),(491168,484802,125),(496194,489828,126),(700730,438816,127),(506244,499878,128),(487147,504904,129),(492172,485806,130),(535259,273345,131),(502222,495857,132),(507248,500882,133),(488150,505907,134),(493175,486809,135),(576545,314631,136),(503226,496860,137),(508251,501885,138),(489153,506911,139),(494179,487813,140),(617831,355916,141),(504229,497863,142),(485132,502889,143),(490157,507914,144),(495182,488816,145),(659117,397202,146),(505233,498867,147),(486135,503892,148),(491160,484795,149),(496186,489820,150),(700402,438488,151),(506236,499870,152),(487139,504896,153),(492164,485798,154),(534931,273017,155),(502214,495849,156),(507240,500874,157),(488142,505899,158),(493167,486801,159),(576217,314303,160),(503218,496852,161),(508243,501877,162),(489146,506903,163),(494171,487805,164),(617503,355589,165),(504221,497855,166),(485124,502881,167),(490149,507906,168),(495174,488808,169),(658789,396874,170),(505225,498859,171),(486127,503884,172),(491152,484787,173),(496178,489812,174),(700075,438160,175),(506228,499862,176),(487131,504888,177),(492156,485790,178),(534603,272689,179),(502206,495841,180),(507232,500866,181),(488134,505891,182),(493159,486793,183),(575889,313975,184),(503210,496844,185),(508235,501869,186),(489138,506895,187),(494163,487797,188),(617175,355261,189),(504213,497848,190),(485116,502873,191),(490141,507898,192),(495166,488800,193),(658461,396547,194),(505217,498851,195),(486119,503876,196),(491144,484779,197),(496170,489804,198),(699747,437833,199),(490205,507962,200),(495230,488864,201),(661084,399169,202),(505281,498915,203),(486183,503940,204),(491208,484842,205),(496234,489868,206),(702370,440455,207),(506284,499918,208),(487186,504943,209),(492212,485846,210),(536898,274984,211),(502262,495896,212),(507288,500922,213),(488190,505947,214),(493215,486849,215),(578184,316270,216),(503266,496900,217),(508291,501925,218),(489193,506950,219),(494219,487853,220),(619470,357556,221),(504269,497903,222),(485171,502929,223),(490197,507954,224),(495222,488856,225),(660756,398842,226),(505273,498907,227),(486175,503932,228),(491200,484834,229),(496226,489860,230),(702042,440127,231),(506276,499910,232),(487178,504936,233),(492204,485838,234),(536570,274656,235),(502254,495888,236),(507280,500914,237),(488182,505939,238),(493207,486841,239),(577856,315942,240),(503258,496892,241),(508283,501917,242),(489185,506942,243),(494211,487845,244),(619142,357228,245),(504261,497895,246),(485164,502921,247),(490189,507946,248),(495214,488848,249),(660428,398514,250),(505265,498899,251),(486167,503924,252),(491192,484826,253),(496218,489852,254),(701714,439800,255),(506268,499902,256),(487170,504928,257),(492196,485830,258),(536243,274328,259),(502246,495880,260),(507272,500906,261),(488174,505931,262),(493199,486833,263),(577528,315614,264),(503250,496884,265),(508275,501909,266),(489177,506934,267),(494203,487837,268),(618814,356900,269),(504253,497887,270),(485156,502913,271),(490181,507938,272),(495206,488840,273),(660100,398186,274),(505257,498891,275),(486159,503916,276),(491184,484818,277),(496210,489844,278),(701386,439472,279),(506260,499894,280),(487162,504920,281),(492188,485822,282),(535915,274000,283),(502238,495872,284),(507264,500898,285),(488166,505923,286),(493191,486825,287),(577201,315286,288),(503242,496876,289),(508267,501901,290),(489169,506927,291),(494195,487829,292),(618486,356572,293),(504245,497879,294),(485148,502905,295),(490173,507930,296),(495198,488832,297),(659772,397858,298),(505249,498883,299),(486151,503908,300),(491176,484810,301),(496202,489836,302),(701058,439144,303),(506252,499886,304),(487155,504912,305),(492180,485814,306),(535587,273673,307),(502230,495864,308),(507256,500890,309),(488158,505915,310),(493183,486817,311),(576873,314958,312),(503234,496868,313),(508259,501893,314),(489161,506919,315),(494187,487821,316),(618159,356244,317),(504237,497871,318),(485140,502897,319),(490165,507922,320),(495190,488824,321),(659444,397530,322),(505241,498875,323),(486143,503900,324),(491168,484802,325),(496194,489828,326),(700730,438816,327),(506244,499878,328),(487147,504904,329),(492172,485806,330),(535259,273345,331),(502222,495857,332),(507248,500882,333),(488150,505907,334),(493175,486809,335),(576545,314631,336),(503226,496860,337),(508251,501885,338),(489153,506911,339),(494179,487813,340),(617831,355916,341),(504229,497863,342),(485132,502889,343),(490157,507914,344),(495182,488816,345),(659117,397202,346),(505233,498867,347),(486135,503892,348),(491160,484795,349),(496186,489820,350),(700402,438488,351),(506236,499870,352),(487139,504896,353),(492164,485798,354),(534931,273017,355),(502214,495849,356),(507240,500874,357),(488142,505899,358),(493167,486801,359),(576217,314303,360),(503218,496852,361),(508243,501877,362),(489146,506903,363),(494171,487805,364),(617503,355589,365),(504221,497855,366),(485124,502881,367),(490149,507906,368),(495174,488808,369),(658789,396874,370),(505225,498859,371),(486127,503884,372),(491152,484787,373),(496178,489812,374),(700075,438160,375),(506228,499862,376),(487131,504888,377),(492156,485790,378),(534603,272689,379),(502206,495841,380),(507232,500866,381),(488134,505891,382),(493159,486793,383),(575889,313975,384),(503210,496844,385),(508235,501869,386),(489138,506895,387),(494163,487797,388),(617175,355261,389),(504213,497848,390),(485116,502873,391),(490141,507898,392),(495166,488800,393),(658461,396547,394),(505217,498851,395),(486119,503876,396),(491144,484779,397),(496170,489804,398),(699747,437833,399)]
    for x,y,z in pos:
	query="store(reshape(slice(normal_obs,Z,%d),<oid:int64 NULL,center:bool NULL,polygon:int32 NULL,sumPixel:int64 NULL,avgDist:double NULL,point:bool NULL>[J=%d:%d,%d,0,I=%d:%d,%d,0]),normal_obs_%d)" %(z,x,x+size-1,size,y,y+size-1,size,z)    
	print query
    	db.executeQuery(query,"afl");
	if(z==19):
		break
    db.disconnect()     #Disconnect from the SciDB server. 

    print "Done!"
    sys.exit(0) #success
if __name__ == "__main__":
    main()
