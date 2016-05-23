<?
include "/htdocs/phplib/phyinf.php";
include "/htdocs/phplib/xnode.php";
$wan1_infp = XNODE_getpathbytarget("", "inf", "uid", "WAN-1", 0);
$opendns_type = query($wan1_infp."/open_dns/type");

$wan1_inet = query($wan1_infp."/inet/");
$inet_p = XNODE_getpathbytarget("inet", "entry", "uid", $wan1_inet, 0);
$ppp_server_ip = query($inet_p."/ppp4/pptp/server");

fwrite("w",$START, "#!/bin/sh\n");
fwrite("w",$STOP,  "#!/bin/sh\n");

fwrite("w", "/etc/resolv.conf", "# Auto-Generated\n");
foreach ("/runtime/inf")
{
	$addrtype = query("inet/addrtype");
	if ($addrtype=="ipv4" || $addrtype=="ppp4")
	{
		if (query("inet/".$addrtype."/valid")=="1" && $opendns_type!="family" && $opendns_type!="parent")
		{
			$def = query("defaultroute");
		//	fwrite("a", $START, "#def: ".$def."\n");
			$uid = query("uid");
			if ($addrtype=="ipv4")		{ $gw = query("inet/".$addrtype."/gateway"); }
			else if ($addrtype=="ppp4")	{ $gw = query("inet/".$addrtype."/peer"); }
			foreach ("inet/".$addrtype."/dns")
			{
				fwrite("a", "/etc/resolv.conf", "nameserver ".$VaLuE."\n");
				if ($def!="" && $def>1)
				{
					if ($gw!="")
					{
						//hendry, workaround for loopback issue (if dns is same as ppp server ip)
						if($addrtype == "ppp4" &&  $ppp_server_ip==$VaLuE) { continue; }
						fwrite(a,$START,'ip route add '.$VaLuE.' via '.$gw.' metric '.$def.' table RESOLV\n');
						fwrite(a,$STOP, 'ip route del '.$VaLuE.' via '.$gw.' metric '.$def.' table RESOLV\n');
					}
					else
					{
						fwrite(a,$START,'ip route add '.$VaLuE.' metric '.$def.' table RESOLV\n');
						fwrite(a,$STOP, 'ip route del '.$VaLuE.' metric '.$def.' table RESOLV\n');
					}
				}
				else
				{
					if ($gw!="")
					{
						fwrite(a,$START,'ip route add '.$VaLuE.' via '.$gw.' table RESOLV\n');
						fwrite(a,$STOP, 'ip route del '.$VaLuE.' via '.$gw.' table RESOLV\n');
					}
					else
					{
						fwrite(a,$START,'ip route add '.$VaLuE.' table RESOLV\n');
						fwrite(a,$STOP, 'ip route del '.$VaLuE.' table RESOLV\n');
					}
				}
			}
		}
	}
	if ($addrtype=="ipv6" || $addrtype=="ppp6")
	{
		if (query("inet/".$addrtype."/valid")=="1")
		{
			$def = query("defaultroute");
		//	fwrite("a", $START, "#def: ".$def."\n");
			$uid = query("uid");
			if ($addrtype=="ipv6")		{ $gw = query("inet/".$addrtype."/gateway"); }
			else if ($addrtype=="ppp6")	{ $gw = query("inet/".$addrtype."/peer"); }
			foreach ("inet/".$addrtype."/dns")
			{
				fwrite("a", "/etc/resolv.conf", "nameserver ".$VaLuE."\n");
				if ($def!="" && $def>1)
				{
					if ($gw!="")
					{
						fwrite(a,$START,'ip -6 route add '.$VaLuE.' via '.$gw.' metric '.$def.' table RESOLV\n');
						fwrite(a,$STOP, 'ip -6 route del '.$VaLuE.' via '.$gw.' metric '.$def.' table RESOLV\n');
					}
					else
					{
						fwrite(a,$START,'ip -6 route add '.$VaLuE.' metric '.$def.' table RESOLV\n');
						fwrite(a,$STOP, 'ip -6 route del '.$VaLuE.' metric '.$def.' table RESOLV\n');
					}
				}
				else
				{
					if ($gw!="")
					{
						fwrite(a,$START,'ip -6 route add '.$VaLuE.' via '.$gw.' table RESOLV\n');
						fwrite(a,$STOP, 'ip -6 route del '.$VaLuE.' via '.$gw.' table RESOLV\n');
					}
					else
					{
						fwrite(a,$START,'ip -6 route add '.$VaLuE.' table RESOLV\n');
						fwrite(a,$STOP, 'ip -6 route del '.$VaLuE.' table RESOLV\n');
					}
				}
			}
		}
	}
}

if ($opendns_type == "family" || $opendns_type == "parent")
{
	fwrite("a", "/etc/resolv.conf", "nameserver ".query($wan1_infp."/open_dns/".$opendns_type."_dns_srv/dns1")."\n");
	fwrite("a", "/etc/resolv.conf", "nameserver ".query($wan1_infp."/open_dns/".$opendns_type."_dns_srv/dns2")."\n");
	fwrite(a,$START,'ip route add '.query($wan1_infp."/open_dns/".$opendns_type."_dns_srv/dns1").' table RESOLV\n');
	fwrite(a,$START,'ip route add '.query($wan1_infp."/open_dns/".$opendns_type."_dns_srv/dns2").' table RESOLV\n');
	fwrite(a,$STOP,'ip route add '.query($wan1_infp."/open_dns/".$opendns_type."_dns_srv/dns1").' table RESOLV\n');
	fwrite(a,$STOP,'ip route add '.query($wan1_infp."/open_dns/".$opendns_type."_dns_srv/dns2").' table RESOLV\n');
}
fwrite("a",$START,"service DNS restart\n");
fwrite("a",$START,"exit 0\n");
fwrite("a",$STOP, "exit 0\n");
?>
