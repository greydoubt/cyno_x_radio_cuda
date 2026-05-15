#ifndef __STACK_HPP_
#define __STACK_HPP_

template <class T> grow_stack        // stack does not shrink
{ 
  public :
  T **sdata;
  long ssize,son;

  grow_stack() { sdata=NULL; ssize=0; son=0; }
  void *push(T *data) 
  { if (son>=ssize) 
    { ssize+=0x100;
      sdata=(T **)jrealloc(sdata,sizeof(T *)*ssize,"stack");
    }
    sdata[son++]=data;
  }
   
  T *pop(long total) 
  { if (son>total) { lbreak("stack underflow\n"); exit(0); }
    son-=total;
    return sdata[son];
  }
} ;

#endif


#include "sock.hpp"
#include "dprint.hpp"

#include <stdlib.h>





const char notify_signature[] = "GET A JOB BOZO";
const char notify_response[] = "\0";

net_protocol *net_protocol::first=0;

// connect to an explictedly named address
// first try to get the address and then try to connect
// return NULL if either fail.  This method does not need to be implemented
// in sub-classes

net_socket *net_protocol::connect_to_server(char *&server_name, int port, int force_port, net_socket::socket_type sock_type)
{
  net_address *a=get_node_address(server_name,port,force_port);
  if (!a) return NULL;
  net_socket *s=connect_to_server(a,sock_type);
  delete a;
  return s;
}



class bwip_address : public net_address
{

  public :
  unsigned long ip;
  unsigned short port;


  virtual protocol protocol_type() { return net_address::IP; }
  virtual equal(net_address *who)
  {
    if (who->protocol_type()==IP && ((bwip_address *)who)->ip==ip)
      return 1;
    else return 0;
  }
  virtual int set_port(int Port)  { port=Port; }
  bwip_address(unsigned long ip, unsigned short port) : port(port), ip(ip) {};
  virtual void print()
  {
    unsigned char *c=(unsigned char *) (&ip);
    dprintf("%d.%d.%d.%d",c[0],c[1],c[2],c[3]);
  }
  bwip_address() {} ;    
} ;

class bw_tcpip_protocol : public net_protocol
{
  unsigned long bw_get_server_ip(char *sn);
  int 
  public :

  
  bw_tcpip_protocol();
  net_address *get_node_address(char *&server_name, int def_port, int force_port);
  net_socket *connect_to_server(net_address *addr, 
				net_socket::socket_type sock_type=net_socket::SOCKET_SECURE);
  net_socket *create_listen_socket(int port, net_socket::socket_type sock_type);
  int select_sockets();
} ;

extern bw_tcpip_protocol tcpip;

class unix_fd : public net_socket
{
  protected :
  int fd;
  public :
  unix_fd(int fd) : fd(fd) { };
  virtual int error()                             { return FD_ISSET(fd,&tcpip.exception_set); }
  virtual int ready_to_read()                     { return FD_ISSET(fd,&tcpip.read_set); }
  virtual int ready_to_write()                    
  { 
    struct timeval tv={0,0};     // don't wait
    fd_set write_check;  
    FD_ZERO(&write_check);  
    FD_SET(fd,&write_check);     
    select(FD_SETSIZE,NULL,&write_check,NULL,&tv);
    return FD_ISSET(fd,&write_check); 
  }
  virtual int write(void *buf, int size)          { return ::write(fd,buf,size); }
  virtual int read(void *buf, int size, net_address **addr)
  {
    int tr=::read(fd,buf,size);
    if (addr) *addr=NULL;
    return tr;
  }

  virtual ~unix_fd()                            { read_unselectable();  write_unselectable(); close(fd); }
  virtual void read_selectable()                   { FD_SET(fd,&tcpip.master_set); }
  virtual void read_unselectable()                 { FD_CLR(fd,&tcpip.master_set); }
  virtual void write_selectable()                  { FD_SET(fd,&tcpip.master_write_set); }
  virtual void write_unselectable()                { FD_CLR(fd,&tcpip.master_write_set); }
  int get_fd() { return fd; }
} ;




unsigned long bw_tcp_protocol::bw_get_server_ip(char *sn)
{
  int a,b,c,d;
  if (sscanf(sn,"%d.%d.%d.%d",&a,&b,&c,&d)==4 && 
      a>=0 && b>=0 && c>=0 && d>=0 &&
      a<256 && b<256 && c<256 && d<256)
  {
    unsigned char c[4];
    c[0]=a;  c[1]=b;  c[2]=c;  c[3]=d;
    return ((unsigned long *)c);
  }
  return 0;
}


bw_tcp_protocol::bw_tcpip_protocol()
{
}

net_address *bw_tcp_protocol::get_node_address(char *&server_name, int def_port, int force_port)
{
  char name[256],*np;
  np=name;
  while (*server_name && *server_name!=':' && *server_name!='/')
    *(np++)=*(server_name)++;
  *np=0;
  if (*server_name==':')
  {
    server_name++;
    char port[256],*p;
    p=port;
    while (*server_name && *server_name!='/')
      *(p++)=*(server_name++);
    *p=0;
    int x;
    if (!force_port)
    {
      if (sscanf(port,"%d",&x)==1) def_port=x;
      else return 0;
    }
  }

  if (*server_name=='/') server_name++;
  unsigned long x=bw_get_server_ip(server_name);
  if (x) 
    return new bwip_address(x,def_port);
  else return NULL;
}


net_socket *bw_tcp_protocol::connect_to_server(net_address *addr, 
					       net_socket::socket_type sock_type=net_socket::SOCKET_SECURE)
{
  if (sock_type==net_socket::SOCKET_SECURE)
  {
    
int bwt_socket()
{
  memset(&rm,0,sizeof(rm));

  strcpy((char *)low_sock_mem, "TCP-IP10");
  rm.eax = 0x3d42;
  rm.ds = low_sock_mem_seg;
  rm.edx = low_sock_mem_off;
  RM_intr(0x21,&rm);
  if (rm.flags & 1)
  {
    current_sock_err=SOCK_CREATE_FAIL;
    sprintf(last_sock_err,"Unable to create socket");    
    return -1;
  }  
  int fd = (rm.eax&0xffff);


  rm.eax=0x4401;
  rm.edx=0x60;
  rm.ebx=fd; 
  RM_intr(0x21,&rm);


  // internal layer must be told to reclaim internal buffers
  low_sock_mem[0]=6;
  low_sock_mem[1]=0x80;
  bwt_ioctl_write(fd, 2);
  return fd;

}


  } else
  {
  }
}

  net_socket *create_listen_socket(int port, net_socket::socket_type sock_type);
  int select_sockets();
