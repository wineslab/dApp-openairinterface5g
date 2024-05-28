import socket,os,sys, struct
import numpy as np
import array,math,time,matplotlib
import matplotlib.pyplot as plt
import threading


###  Configuration ###

#gNB runs with BW = 40 MHz, with -E (3/4 sampling)
# No SRS for now, hence in gNB config file set do_SRS = 0
# gNB->frame_parms.ofdm_symbol_size = 1536
# gNB->frame_parms.first_carrier_offset = 900
# Noise floor threshold needs to be calibrated
# We receive the symbols and average them over some frames,
# and do thresholding.

FFT_SIZE = 1536
First_carrier_offset = 900
Noise_floor_threshold = 31
Average_over_frames = 127
Num_car_prb = 12


# message format: length of the buffer (4 bytes) + buffer 
def recv_message(conn):
  sense_symbol = b''
  num_bytes=conn.recv(4)
  buf_size = int.from_bytes(num_bytes,'big')
  #print(f"size = {buf_size}")

  while(buf_size > 0):
    iq_buf = conn.recv(buf_size)
    sense_symbol = sense_symbol + iq_buf
    buf_size = buf_size - len(iq_buf)

  #print(len(sense_symbol))
  return sense_symbol

def prb_update(prb_blk_list,n):
  #print(prb_blk_list)
  #prb_blk_list.astype('f').tostring()
  #array2=prb_blk_list
  array2=prb_blk_list.tobytes(order='C')
  #array2=bin
  #print(f"length {n}")
  array1 =  n.to_bytes(2, 'little') 
  #print(array1)
  gnb_server.send(array1+array2)




def run_server():

  server_ip = "127.0.0.1"
  port = 9990

  server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
  server.bind((server_ip, port))
  
  server.listen(5)
  print(f"Listening on {server_ip}:{port}")
  
  client_socket, client_address = server.accept()
  print(f"Accepted connection from {client_address[0]}:{client_address[1]}")

  
  plt.ion()
  # here we are creating sub plots
  figure, ax = plt.subplots(figsize=(10, 8))
  #ax.set_ylim([0, 5])
  x=np.arange(FFT_SIZE)
  y=np.arange(FFT_SIZE)
  line1, = ax.plot(x, y)  
  ax.set_ylim([0, 80])
  plt.title("Spectrum Sensing at gNB (Carrier @3.619 GHz, BW = 40 MHz)", fontsize=18)
  plt.xlabel("Subcarrier", fontsize=12)
  plt.ylabel("Energy [dB]", fontsize=12)

  abs_iq_av = np.zeros(FFT_SIZE)
  count = 1
  while True:
    iq_arr=(np.frombuffer(recv_message(client_socket), np.int16))
    iq_comp = iq_arr[::2] + iq_arr[1::2]* 1j
    abs_iq = abs(iq_comp).astype(float)
    abs_iq_av = abs_iq_av + abs_iq
    count += 1

    if(count == Average_over_frames):
      abs_iq_av_db =  20*np.log10(1+(abs_iq_av/(Average_over_frames)))
      abs_iq_av_db_shift = np.append(abs_iq_av_db[FFT_SIZE//2:FFT_SIZE],abs_iq_av_db[0:FFT_SIZE//2]) 
      abs_iq_av_db_offset_correct = np.append(abs_iq_av_db[First_carrier_offset:FFT_SIZE],abs_iq_av_db[0:First_carrier_offset])
      f_ind = np.arange(FFT_SIZE)
      blklist_sub_carier = f_ind[abs_iq_av_db_offset_correct > Noise_floor_threshold]
      np.sort(blklist_sub_carier)
      prb_blk_list=np.unique((np.floor(blklist_sub_carier/Num_car_prb))).astype(np.uint16)
      prb_blk_list = prb_blk_list[prb_blk_list>75]
      #print(prb_blk_list)
      prb_new = prb_blk_list.newbyteorder('>')

      t1 = threading.Thread(target=prb_update, args=(prb_new,prb_blk_list.size,))
      t1.start()  
      abs_iq_av = np.zeros(FFT_SIZE)
      count = 1
      line1.set_xdata(x)
      line1.set_ydata(abs_iq_av_db_shift)
      figure.canvas.draw()
      figure.canvas.flush_events()   

  # close connection socket with the client
  client_socket.close()
  print("Connection to client closed")
  # close server socket
  server.close()
    
if __name__ == '__main__':
  #Creating a client to send PRB updates
  gnb_server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
  gnb_ip = "127.0.0.1"
  gnb_port = 9999
  gnb_server.connect((gnb_ip, gnb_port))
  run_server()

