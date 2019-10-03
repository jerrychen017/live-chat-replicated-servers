Minqi Ma (JHED: mma17)\
Guangrui Chen (JHED: gchen41)

# Initial Design

### Main Idea
* We are using lamport timestamps since it can handle higher loss rate


### Internal Data Structures 
* internal counters for each machine
* timeout
* window

### Types and Structure of Messages
Packet
* machine_index
* packet_index 
* random number
* data

Message
* machine_index
* packet_index
 
### Protocal description 
* All procsses internal counters are initializd to 0. 
* Each process sends packets to all other processes
* When a process sends a packet, it increments the internal counter and updates the packet packet_index based on its internal counter for the same machine. 
* After sending a packet, the process waits for messages (acks) from all other processes. Once received an ack from all other processes, it sends the next packet. If timeout, it sends the packet again. This is not really efficient. We can send a window of packets in order. Then if we received acks from all processes with the same packet_index, then we can slide the window to that index. 
* When a process receives a packet, if an internal counter for the machine_index of the packet doesn't exist, create an internal counter for the machine with this machine_index. The internal counter is updated to max(packet_index, internal counter). The process increments the internal counter by 1. Then, the process sends the packet to all other processes.
* After receiving a packet, the process sends a message to the machine that corresponds to the machine_index of the packet. The mesasage contains the process's internal counter for that machine. 
* A process should terminate once received messages from all other processes with the last index.

