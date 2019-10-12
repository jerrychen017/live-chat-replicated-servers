Minqi Ma (JHED: mma17)\
Guangrui Chen (JHED: gchen41)

# Initial Design

### Main Idea and notes 
* We are using lamport timestamps since it can handle higher loss rate
* counter serves the purpose of timestamp 
* names of machine and process is interchangable

When to deliver 
When to nack
When to send 

### Internal Data Structures 
* internal counter (timestamp of all packets)
* packet_index (keep track of packets this machine sends)
* timeout for each of other processes
* buffer for packets to be delivered


### Types and Structure of Messages
Packet 
* machine_index
* packet_index 
* counter (timestamp)
* random number
* data (garbage data)
* counter(timestamp that all other processes has delivered)

Message
* machine_index
* packet_index
* query_machine_index
* tag (timeout, terminate, nack)
 
### Protocal description 

Ordering: 
Counter comes first. If there's a tie on counters, order by machine_index

#### Initialization
* All procsses internal counters are initializd to 0. 
* Every process should send counters
* 
* initialize a window for all received packets
* All processes initialize \<number of machine>  timeouts
* run start_mcast on one machine (let's say p1 for example) to signal all processes to start. More specifically, p0 sends all other processes a special packet with index 0. All other processes receive this packet using recv (no loss). 

#### Algorithm 
##### Create a packet
* A machine creates a packet by assigning a random number from the range [1,1000000] 
* Add machine_index to this packet
* Add current internal counter to this packet
* Store packet in the window of this machine

##### Sending new packet
* increment internal counter by 1
* create a packet
* send the packet to all other processes

##### Receiving a packet
* increment internal counter by 1
* Adopt the counter of the packet if it's bigger than the internal counter
* Put the packet in receiving window
* create a pointer to the packet and put the pointer in the window of the corresponding machine (by checking of machien_index of the packet). 

##### timeout 
* If didn't receive a packet with a certain machine_index until timeout, we send a message to that machine with the machine_index and the last packet_index received from this machine. 
* we send timeout message to all other processes. 

##### missing packets
* Among windows of pointers, if any window has non-consecutive packets (for example for the windows that correspondes to machine 1, there's a packet with packet_index 3 after a packet with packet_index 1), then we know there is a missing packet with certain packet_index and machine_index. 
* Create a message (nack) with such packet_index and machine_index. 
* add this machine index as query_machine_index to the message
* Send the message to all other processes. 

##### receiving a timeout
* 

##### receiving a nack
* go to the corresponding window and search for the packet (if this machine is the machine that generates the packet, then it must have it)
* send the packet to the machine with \<query_machine_index>


#### Termination




* Each process sends packets to all other processes
* When a process sends a packet, it increments the internal counter and updates the packet packet_index based on its internal counter for the same machine. 
* After sending a packet, the process waits for messages (acks) from all other processes. Once received an ack from all other processes, it sends the next packet. If timeout, it sends the packet again. This is not really efficient. We can send a window of packets in order. Then if we received acks from all processes with the same packet_index, then we can slide the window to that index. 
* When a process receives a packet, if an internal counter for the machine_index of the packet doesn't exist, create an internal counter for the machine with this machine_index. The internal counter is updated to max(packet_index, internal counter). The process increments the internal counter by 1. Then, the process sends the packet to all other processes.
* After receiving a packet, the process sends a message to the machine that corresponds to the machine_index of the packet. The mesasage contains the process's internal counter for that machine. 
* A process should terminate once received messages from all other processes with the last index.



QUESTIONS
- start_mcast (separate terminal??)
- GOAL 
- upon receiving a packet, update the packet's counter if internal couner is larger?? 
- 

