
#include <yat/threading/Mutex.h>
#include <sstream>
#include <iostream>
#include <string>
#include <math.h>
#include "Debug.h"
#include "Data.h"
#include "AdscReader.h"
#include "AdscInterface.h"


//---------------------------
//- Ctor
//---------------------------
Reader::Reader(Camera& cam, HwBufferCtrlObj& buffer_ctrl)
      : m_cam(cam),
        m_buffer(buffer_ctrl),
        m_stop_already_done(true),
        m_dw(0)
{
    DEB_CONSTRUCTOR();
    try
    {
      m_image_number = 0;
      m_time_out_watcher = 5;
      enable_timeout_msg(false);
      enable_periodic_msg(false);
      set_periodic_msg_period(kTASK_PERIODIC_TIMEOUT_MS);
      m_cam.getMaxImageSize(m_image_size);
    }
    catch (Exception &e)
    {
      // Error handling
      DEB_ERROR() << e.getErrMsg();
      throw LIMA_HW_EXC(Error, e.getErrMsg());
    }    
}

//---------------------------
//- Dtor
//---------------------------
Reader::~Reader()
{
    DEB_DESTRUCTOR();
    try
    {
      if(m_dw!=0)
      {
        delete m_dw;
        m_dw = 0;
      }
    }
    catch (Exception &e)
    {
      // Error handling
      DEB_ERROR() << e.getErrMsg();
      throw LIMA_HW_EXC(Error, e.getErrMsg());
    }
}

//---------------------------
//- Reader::start()
//---------------------------
void Reader::start()
{
    DEB_MEMBER_FUNCT();
    try
    {
      if(m_dw!=0)
      {
        delete m_dw;
        m_dw = 0;
      }
      m_dw = new gdshare::DirectoryWatcher(/*m_cam.getDirectoryWatcherPath()*/"/home/experiences/proxima2a/com-proxima2a/DeviceServers/my_images/");
      this->post(new yat::Message(ADSC_START_MSG), kPOST_MSG_TMO);
    }
    catch (Exception &e)
    {
      // Error handling
      DEB_ERROR() << e.getErrMsg();
      throw LIMA_HW_EXC(Error, e.getErrMsg());
    }
    catch( yat::Exception& ex )
    {
      DEB_ERROR() <<"Error : " << ex.errors[0].desc;
      throw LIMA_HW_EXC(Error, ex.errors[0].desc);
    }
}

//---------------------------
//- Reader::stop()
//---------------------------
void Reader::stop(bool immediatley)
{
  DEB_MEMBER_FUNCT();
  try
  {		
		yat::Message* msg = new yat::Message(ADSC_STOP_MSG);
    msg->attach_data( (bool)immediatley );
    this->post(msg, kPOST_MSG_TMO);
  }
  catch( yat::Exception& ex )
  {
    DEB_ERROR() <<"Error : " << ex.errors[0].desc;
    throw LIMA_HW_EXC(Error, ex.errors[0].desc);
  }
}

//---------------------------
//- Reader::reset()
//---------------------------
void Reader::reset()
{
  DEB_MEMBER_FUNCT();
  try
  {
    this->post(new yat::Message(ADSC_RESET_MSG), kPOST_MSG_TMO);
  }
  catch( yat::Exception& ex )
  {
    DEB_ERROR() <<"Error : " << ex.errors[0].desc;    
    throw LIMA_HW_EXC(Error, ex.errors[0].desc);
  }
}

//---------------------------
//- Reader::getLastAcquiredFrame()
//---------------------------
int Reader::getLastAcquiredFrame(void)
{
  {
    yat::MutexLock scoped_lock(lock_);
    return m_image_number;        
  }
}

//-----------------------------------------------------
//
//-----------------------------------------------------
void Reader::handle_message( yat::Message& msg )  throw( yat::Exception )
{
  DEB_MEMBER_FUNCT();
  try
  {
    switch ( msg.type() )
    {
      //-----------------------------------------------------    
      case yat::TASK_INIT:
      {
        DEB_TRACE() <<"Reader::->TASK_INIT";
      }
      break;
      //-----------------------------------------------------    
      case yat::TASK_EXIT:
      {
        DEB_TRACE() <<"Reader::->TASK_EXIT";
      }
      break;
      //-----------------------------------------------------    
      case yat::TASK_TIMEOUT:
      {
        DEB_TRACE() <<"Reader::->TASK_TIMEOUT";
      }
      break;
      //-----------------------------------------------------    
      case yat::TASK_PERIODIC:
      {
        DEB_TRACE() <<"Reader::->TASK_PERIODIC";
        if(m_stop_already_done)
        {            
          if(m_elapsed_seconds_from_stop >= m_time_out_watcher)// TO
          {
            enable_periodic_msg(false);
            if(m_dw!=0)
            {
              delete m_dw;
              m_dw = 0;
            }                     
            return;
          }
          m_elapsed_seconds_from_stop ++;
          DEB_TRACE()<<"Elapsed seconds since stop() = " <<m_elapsed_seconds_from_stop<< " s";
        }

        bool continueAcq = false;

        StdBufferCbMgr& buffer_mgr = ((reinterpret_cast<BufferCtrlObj&>(m_buffer)).getBufferCbMgr());
        
        if(m_dw)
        {
          gdshare::FileNamePtrVector vecNewAndChangedFiles;
          m_dw->GetChanges(&vecNewAndChangedFiles,&vecNewAndChangedFiles);

          for(int i= 0;i<vecNewAndChangedFiles.size();i++)
          {
            DEB_TRACE() <<"vecNewAndChangedFiles = "<< vecNewAndChangedFiles.at(i)->FullName();
            if(vecNewAndChangedFiles.at(i)->FileExists())
            {
              DEB_TRACE()  << "image#" << m_image_number <<" acquired !";
              DEB_TRACE()  << "file : " << vecNewAndChangedFiles.at(i)->FullName();
              
              DEB_TRACE()<<"-- Read an image using DI";
              m_DI = new DI::DiffractionImage(const_cast<char*>(vecNewAndChangedFiles.at(i)->FullName().c_str()));
                             
              if(m_image_size.getWidth()!=m_DI->getWidth() || m_image_size.getHeight()!=m_DI->getHeight())
                throw LIMA_HW_EXC(Error, "Image size in file is different from the expected image size of this detector !");

              DEB_TRACE()<<"-- prepare memory with image data"; 
              int buffer_nb, concat_frame_nb;        
              buffer_mgr.setStartTimestamp(Timestamp::now());
              buffer_mgr.acqFrameNb2BufferNb(m_image_number, buffer_nb, concat_frame_nb);
              
              void *ptr = buffer_mgr.getBufferPtr(buffer_nb,   concat_frame_nb);

              for(int j=0;j<m_DI->getWidth()*m_DI->getHeight();j++)
              {
                ((uint16_t*)ptr)[j] = (uint16_t)( m_DI->getImage()[j]);
              }

              DEB_TRACE()<<"-- newFrameReady";                      
              HwFrameInfoType frame_info;
              frame_info.acq_frame_nb = m_image_number;
              continueAcq = buffer_mgr.newFrameReady(frame_info);
              
              int nb_frames = 0;
              m_cam.getNbFrames(nb_frames);
              // if nb acquired image < requested frames
              if (continueAcq && (!nb_frames||m_image_number<nb_frames))
              {
                yat::MutexLock scoped_lock(lock_);
                {
                  m_image_number++;
                }
              }
              else
              {
                stop();
                return;
              }
            }
          }
        }
      }
      break;
      //-----------------------------------------------------    
      case ADSC_START_MSG:    
      {
        DEB_TRACE() << "Reader::->ADSC_START_MSG";
        m_image_number = 0;
        m_elapsed_seconds_from_stop = 0;
        m_stop_already_done = false;        
        enable_periodic_msg(true);
      }
      break;
      //-----------------------------------------------------
      case ADSC_STOP_MSG:
      {
        DEB_TRACE() << "Reader::->ADSC_STOP_MSG";
        bool bStopImediatley = msg.get_data<bool>();
        if(bStopImediatley)
          m_time_out_watcher = 1;
        else
          m_time_out_watcher = 5;
        if(!m_stop_already_done)
        {          
          m_elapsed_seconds_from_stop = 0;
          m_stop_already_done = true;
        }
      }
      break;
      //-----------------------------------------------------
      case ADSC_RESET_MSG:
      {
        DEB_TRACE() << "Reader::->ADSC_RESET_MSG";
      }
      break;    
      //-----------------------------------------------------
    }
  }
  catch( yat::Exception& ex )
  {
    DEB_ERROR() <<"Error : " << ex.errors[0].desc;    
    throw LIMA_HW_EXC(Error, ex.errors[0].desc);
  }

}

//-----------------------------------------------------