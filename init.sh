!/bin/sh

cd ~

sudo apt-get install autoconf libtool build-essential pkg-config libevent-dev libssl-dev libpcap-dev git automake libsnmp-dev libconfig8-dev

wget http://sourceforge.net/projects/boost/files/boost/1.55.0/boost_1_55_0.tar.bz2/download
tar xjvf download 
mv download boost_1_55_0/archive.tar.bz2

git clone https://github.com/OpenNetworkingFoundation/libfluid.git
cd libfluid
./bootstrap.sh

cd libfluid_base
./configure --prefix=/usr
make
sudo make install

cd ..

cd libfluid_msg
./configure --prefix=/usr
make
sudo make install

cd ~

git clone git://gitosis.stanford.edu/openflow.git
cd openflow; git checkout -b mybranch origin/release/1.0.0
git clone git://gitosis.stanford.edu/oflops.git
cd oflops ; sh ./boot.sh ; ./configure --with-openflow-src-dir=$HOME/openflow; make; sudo make install

cd ~/m2
