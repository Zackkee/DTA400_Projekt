#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/point-to-point-module.h"
#include "ns3/applications-module.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/flow-monitor-module.h"
#include <sstream>
#include <fstream>

using namespace ns3;

// Funktion som loggar mottagna paket med tidsstämplar till CSV
void LogRx (std::ofstream *outFile, Ptr<const Packet> p, const Address &addr)
{
  *outFile << Simulator::Now ().GetSeconds () << "," << p->GetSize () << "\n";
}

int main (int argc, char *argv[])
{
  // 1. Skapa 5 noder: Attackerare, UDP-användare, TCP-användare, Router, Server
  NodeContainer nodes;
  nodes.Create (5);

  Ptr<Node> attacker = nodes.Get (0);
  Ptr<Node> userUdp = nodes.Get (1);
  Ptr<Node> userTcp = nodes.Get (2);
  Ptr<Node> router = nodes.Get (3);
  Ptr<Node> server = nodes.Get (4);

  // 2. Sätt upp snabba länkar och en trång flaskhalslänk mot servern (1 Mbps)
  PointToPointHelper fastLink;
  fastLink.SetDeviceAttribute ("DataRate", StringValue ("10Mbps"));
  fastLink.SetChannelAttribute ("Delay", StringValue ("2ms"));

  PointToPointHelper bottleneckLink;
  bottleneckLink.SetDeviceAttribute ("DataRate", StringValue ("1Mbps")); // Flaskhals!
  bottleneckLink.SetChannelAttribute ("Delay", StringValue ("10ms"));
  bottleneckLink.SetQueue ("ns3::DropTailQueue", "MaxSize", StringValue ("10p"));

  NetDeviceContainer devAttacker = fastLink.Install (attacker, router);
  NetDeviceContainer devUserUdp = fastLink.Install (userUdp, router);
  NetDeviceContainer devUserTcp = fastLink.Install (userTcp, router);
  NetDeviceContainer devServer = bottleneckLink.Install (router, server);

  // 3. Installera IP-stack
  InternetStackHelper stack;
  stack.Install (nodes);

  Ipv4AddressHelper address;
  address.SetBase ("10.1.1.0", "255.255.255.0");
  Ipv4InterfaceContainer ifAttacker = address.Assign (devAttacker);

  address.SetBase ("10.1.2.0", "255.255.255.0");
  Ipv4InterfaceContainer ifUserUdp = address.Assign (devUserUdp);

  address.SetBase ("10.1.3.0", "255.255.255.0");
  Ipv4InterfaceContainer ifUserTcp = address.Assign (devUserTcp);

  address.SetBase ("10.1.4.0", "255.255.255.0");
  Ipv4InterfaceContainer ifServer = address.Assign (devServer);

  // 4. Servern lyssnar på tre olika portar under hela simuleringen (300 sekunder)
  uint16_t attackerPort = 8080;
  uint16_t userUdpPort = 8082;
  uint16_t userTcpPort = 8081;

  PacketSinkHelper attackerSink ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), attackerPort));
  ApplicationContainer serverAttackerApp = attackerSink.Install (server);
  serverAttackerApp.Start (Seconds (0.0));
  serverAttackerApp.Stop (Seconds (300.0));

  PacketSinkHelper userUdpSink ("ns3::UdpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), userUdpPort));
  ApplicationContainer serverUserUdpApp = userUdpSink.Install (server);
  serverUserUdpApp.Start (Seconds (0.0));
  serverUserUdpApp.Stop (Seconds (300.0));

  PacketSinkHelper userTcpSink ("ns3::TcpSocketFactory", InetSocketAddress (Ipv4Address::GetAny (), userTcpPort));
  ApplicationContainer serverUserTcpApp = userTcpSink.Install (server);
  serverUserTcpApp.Start (Seconds (0.0));
  serverUserTcpApp.Stop (Seconds (300.0));

  // Förbereda CSV-filer för loggning
  std::ofstream tcpFile ("tcp_throughput.csv");
  tcpFile << "Time,PacketSize\n";
  std::ofstream udpFile ("udp_throughput.csv");
  udpFile << "Time,PacketSize\n";
  std::ofstream attFile ("attacker_throughput.csv");
  attFile << "Time,PacketSize\n";

  // Koppla spårning till serverns mottagare (sinks)
  Ptr<PacketSink> tcpSinkApp = DynamicCast<PacketSink> (serverUserTcpApp.Get (0));
  tcpSinkApp->TraceConnectWithoutContext ("Rx", MakeBoundCallback (&LogRx, &tcpFile));

  Ptr<PacketSink> udpSinkApp = DynamicCast<PacketSink> (serverUserUdpApp.Get (0));
  udpSinkApp->TraceConnectWithoutContext ("Rx", MakeBoundCallback (&LogRx, &udpFile));

  Ptr<PacketSink> attSinkApp = DynamicCast<PacketSink> (serverAttackerApp.Get (0));
  attSinkApp->TraceConnectWithoutContext ("Rx", MakeBoundCallback (&LogRx, &attFile));

  // 5. Attackeraren (UDP, 5 Mbps) - Aktiv mellan sekund 60.0 och 240.0
  OnOffHelper attackerApp ("ns3::UdpSocketFactory", InetSocketAddress (ifServer.GetAddress (1), attackerPort));
  attackerApp.SetAttribute ("DataRate", StringValue ("50Mbps")); 
  attackerApp.SetAttribute ("PacketSize", UintegerValue (1024));
  ApplicationContainer app1 = attackerApp.Install (attacker);
  app1.Start (Seconds (60.0)); 
  app1.Stop (Seconds (240.0));  

  // 6. UDP-användare (UDP, 700 kb/s) - Aktiv mellan sekund 10.0 och 290.0
  OnOffHelper udpClientApp ("ns3::UdpSocketFactory", InetSocketAddress (ifServer.GetAddress (1), userUdpPort));
  udpClientApp.SetAttribute ("DataRate", StringValue ("400kb/s"));
  udpClientApp.SetAttribute ("PacketSize", UintegerValue (512));
  ApplicationContainer app2 = udpClientApp.Install (userUdp);
  app2.Start (Seconds (10.0));
  app2.Stop (Seconds (290.0));

  // 7. TCP-användare (TCP, 700 kb/s) - Aktiv mellan sekund 10.0 och 290.0
  OnOffHelper tcpClientApp ("ns3::TcpSocketFactory", InetSocketAddress (ifServer.GetAddress (1), userTcpPort));
  tcpClientApp.SetAttribute ("DataRate", StringValue ("400kb/s"));
  tcpClientApp.SetAttribute ("PacketSize", UintegerValue (512));
  ApplicationContainer app3 = tcpClientApp.Install (userTcp);
  app3.Start (Seconds (10.0));
  app3.Stop (Seconds (290.0));

  Ipv4GlobalRoutingHelper::PopulateRoutingTables ();

  // 8. FlowMonitor
  FlowMonitorHelper flowmon;
  Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

  Simulator::Stop (Seconds (300.0));

  std::cout << "Simuleringen startar (5 minuter / 300 sekunder)..." << std::endl;
  Simulator::Run ();

  // Stäng loggfilerna
  tcpFile.close ();
  udpFile.close ();
  attFile.close ();

  // Mappning av IP till läsbara namn
  auto getHostName = [&](Ipv4Address addr) {
    if (addr == ifAttacker.GetAddress (0)) return std::string ("Attackerare (UDP)");
    if (addr == ifUserUdp.GetAddress (0)) return std::string ("UDP-användare");
    if (addr == ifUserTcp.GetAddress (0)) return std::string ("TCP-användare");
    if (addr == ifServer.GetAddress (1)) return std::string ("Server");
    return std::string ("Router");
  };

  monitor->CheckForLostPackets ();
  Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier> (flowmon.GetClassifier ());
  std::map<FlowId, FlowMonitor::FlowStats> stats = monitor->GetFlowStats ();

  std::cout << "\n================ RESULTAT (5 MINUTERS SIMULERING) ================\n";
  for (auto it = stats.begin (); it != stats.end (); ++it)
    {
      Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow (it->first);
      std::string srcName = getHostName (t.sourceAddress);
      std::string dstName = getHostName (t.destinationAddress);

      std::cout << "Flöde (" << srcName << " -> " << dstName << "):\n";
      std::cout << "  - Skickade paket: " << it->second.txPackets << "\n";
      std::cout << "  - Mottagna paket: " << it->second.rxPackets << "\n";
      std::cout << "  - Tappade paket:  " << it->second.lostPackets << "\n";
      double packetLossRate = (it->second.txPackets > 0) ? 
        ((double)it->second.lostPackets / it->second.txPackets) * 100.0 : 0.0;
      std::cout << "  - Paketförlust:   " << packetLossRate << " %\n";
      std::cout << "-------------------------------------------------------------\n";
    }

  std::cout << "Simuleringen är klar! Tidsdata har sparats till tcp_throughput.csv, udp_throughput.csv och attacker_throughput.csv." << std::endl;
  Simulator::Destroy ();
  return 0;
}
