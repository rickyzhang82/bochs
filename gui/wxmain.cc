/////////////////////////////////////////////////////////////////
// $Id: wxmain.cc,v 1.101 2003/10/24 15:39:57 vruppert Exp $
/////////////////////////////////////////////////////////////////
//
// wxmain.cc implements the wxWindows frame, toolbar, menus, and dialogs.
// When the application starts, the user is given a chance to choose/edit/save
// a configuration.  When they decide to start the simulation, functions in
// main.cc are called in a separate thread to initialize and run the Bochs
// simulator.  
//
// Most ports to different platforms implement only the VGA window and
// toolbar buttons.  The wxWindows port is the first to implement both
// the VGA display and the configuration interface, so the boundaries
// between them are somewhat blurry.  See the extensive comments at
// the top of siminterface for the rationale behind this separation.
//
// The separation between wxmain.cc and wx.cc is as follows:
// - wxmain.cc implements a Bochs configuration interface (CI),
//   which is the wxWindows equivalent of control.cc.  wxmain creates
//   a frame with several menus and a toolbar, and allows the user to
//   choose the machine configuration and start the simulation.  Note
//   that wxmain.cc does NOT include bochs.h.  All interactions
//   between the CI and the simulator are through the siminterface
//   object.
// - wx.cc implements a VGA display screen using wxWindows.  It is 
//   is the wxWindows equivalent of x.cc, win32.cc, macos.cc, etc.
//   wx.cc includes bochs.h and has access to all Bochs devices.
//   The VGA panel accepts only paint, key, and mouse events.  As it
//   receives events, it builds BxEvents and places them into a 
//   thread-safe BxEvent queue.  The simulation thread periodically
//   processes events from the BxEvent queue (bx_gui_c::handle_events)
//   and notifies the appropriate emulated I/O device.
//
//////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////
// includes
//////////////////////////////////////////////////////////////////////

// Define BX_PLUGGABLE in files that can be compiled into plugins.  For
// platforms that require a special tag on exported symbols, BX_PLUGGABLE 
// is used to know when we are exporting symbols and when we are importing.
#define BX_PLUGGABLE

#include "config.h"              // definitions based on configure script
#if BX_WITH_WX

// For compilers that support precompilation, includes "wx/wx.h".
#include <wx/wxprec.h>
#ifdef __BORLANDC__
#pragma hdrstop
#endif
#ifndef WX_PRECOMP
#include <wx/wx.h>
#endif
#include <wx/image.h>
#include <wx/clipbrd.h>

#include "osdep.h"               // workarounds for missing stuff
#include "gui/siminterface.h"    // interface to the simulator
#include "bxversion.h"           // get version string
#include "wxdialog.h"            // custom dialog boxes
#include "wxmain.h"              // wxwindows shared stuff
#include "extplugin.h"

// include XPM icons
#include "bitmaps/cdromd.xpm"
#include "bitmaps/copy.xpm"
#include "bitmaps/floppya.xpm"
#include "bitmaps/floppyb.xpm"
#include "bitmaps/paste.xpm"
#include "bitmaps/power.xpm"
#include "bitmaps/reset.xpm"
#include "bitmaps/snapshot.xpm"
#include "bitmaps/mouse.xpm"
//#include "bitmaps/configbutton.xpm"
#include "bitmaps/userbutton.xpm"
#ifdef __WXGTK__
#include "icon_bochs.xpm"
#endif

// FIXME: ugly global variables that the bx_gui_c object in wx.cc can use
// to access the MyFrame and the MyPanel.
MyFrame *theFrame = NULL;
MyPanel *thePanel = NULL;

// The wxBochsClosing flag is used to keep track of when the wxWindows GUI is
// shutting down.  Shutting down can be somewhat complicated because the
// simulation may be running for a while in another thread before it realizes
// that it should shut down.  The wxBochsClosing flag is a global variable, as
// opposed to a field of some C++ object, so that it will be valid at any stage
// of the program.  wxBochsClosing starts out false (not wxBochsClosing).  When
// the GUI decides to shut down, it sets wxBochsClosing=true.  If there
// is not a simulation running, everything is quite simple and it can just
// call Close(TRUE).  If a simulation is running, it calls OnKillSim to
// ask the simulation to stop.  When the simulation thread stops, it calls
// Close(TRUE) on the frame.  During the time that the simulation is 
// still running and afterward, the wxBochsClosing flag is used to suppress
// any events that might reference parts of the GUI or create new dialogs.
bool wxBochsClosing = false;

bool isSimThread () {
  if (wxThread::IsMain()) return false;
  wxThread *current = wxThread::This ();
  if (current == (wxThread*) theFrame->GetSimThread ()) {
    //wxLogDebug ("isSimThread? yes");
    return true;
  }
  //wxLogDebug ("isSimThread? no");
  return false;
}

//////////////////////////////////////////////////////////////////////
// class declarations
//////////////////////////////////////////////////////////////////////

class MyApp: public wxApp
{
virtual bool OnInit();
virtual int OnExit();
  public:
  // This default callback is installed when the simthread is NOT running,
  // so that events coming from the simulator code can be handled.
  // The primary culprit is panics which cause an BX_SYNC_EVT_LOG_ASK.
  static BxEvent *DefaultCallback (void *thisptr, BxEvent *event);
};

// SimThread is the thread in which the Bochs simulator runs.  It is created
// by MyFrame::OnStartSim().  The SimThread::Entry() function calls a
// function in main.cc called bx_continue_after_config_interface() which
// initializes the devices and starts up the simulation.  All events from
// the simulator
class SimThread: public wxThread
{
  MyFrame *frame;

  // when the sim thread sends a synchronous event to the GUI thread, the
  // response is stored in sim2gui_mailbox.
  // FIXME: this would be cleaner and more reusable if I made a general
  // thread-safe mailbox class.
  BxEvent *sim2gui_mailbox;
  wxCriticalSection sim2gui_mailbox_lock;

public:
  SimThread (MyFrame *_frame) { frame = _frame; sim2gui_mailbox = NULL; }
  virtual ExitCode Entry ();
  void OnExit ();
  // called by the siminterface code, with the pointer to the sim thread
  // in the thisptr arg.
  static BxEvent *SiminterfaceCallback (void *thisptr, BxEvent *event);
  BxEvent *SiminterfaceCallback2 (BxEvent *event);
  // methods to coordinate synchronous response mailbox
  void ClearSyncResponse ();
  void SendSyncResponse (BxEvent *);
  BxEvent *GetSyncResponse ();
};


//////////////////////////////////////////////////////////////////////
// wxWindows startup
//////////////////////////////////////////////////////////////////////

static int ci_callback (void *userdata, ci_command_t command)
{
  switch (command)
  {
    case CI_START:
      //fprintf (stderr, "wxmain.cc: start\n");
#ifdef __WXMSW__
      // on Windows only, wxEntry needs some data that is passed into WinMain.
      // So, in main.cc we define WinMain and fill in the bx_startup_flags
      // structure with the data, so that when we're ready to call wxEntry
      // it has access to the data.
      wxEntry (
            bx_startup_flags.hInstance,
            bx_startup_flags.hPrevInstance,
            bx_startup_flags.m_lpCmdLine,
            bx_startup_flags.nCmdShow);
#else
      wxEntry (bx_startup_flags.argc, bx_startup_flags.argv);
#endif
      break;
    case CI_RUNTIME_CONFIG:
      fprintf (stderr, "wxmain.cc: runtime config not implemented\n");
      break;
    case CI_SHUTDOWN:
      fprintf (stderr, "wxmain.cc: shutdown not implemented\n");
      break;
  }
  return 0;
}

extern "C" int libwx_LTX_plugin_init (plugin_t *plugin, plugintype_t type,
  int argc, char *argv[])
{
  wxLogDebug ("plugin_init for wxmain.cc");
  wxLogDebug ("installing wxWindows as the configuration interface");
  SIM->register_configuration_interface ("wx", ci_callback, NULL);
  wxLogDebug ("installing %s as the Bochs GUI", "wxWindows");
  MyPanel::OnPluginInit ();
  return 0; // success
}

extern "C" void libwx_LTX_plugin_fini ()
{
  //fprintf (stderr, "plugin_fini for wxmain.cc\n");
}


//////////////////////////////////////////////////////////////////////
// MyApp: the wxWindows application
//////////////////////////////////////////////////////////////////////

IMPLEMENT_APP_NO_MAIN(MyApp)

// this is the entry point of the wxWindows code.  It is called as follows:
// 1. main() loads the wxWindows plugin (if necessary) and calls 
// libwx_LTX_plugin_init, which installs a function pointer to the
// ci_callback() function.
// 2. main() calls SIM->configuration_interface.
// 3. bx_real_sim_c::configuration_interface calls the function pointer that
//    points to ci_callback() in this file, with command=CI_START.
// 4. ci_callback() calls wxEntry() in the wxWindows library
// 5. wxWindows library creates the app and calls OnInit().
//
// Before this code is called, the command line has already been parsed, and a
// .bochsrc has been loaded if it could be found.  See main() for details.
bool MyApp::OnInit()
{
  //wxLog::AddTraceMask (_T("mime"));
  wxLog::SetActiveTarget (new wxLogStderr ());
  bx_init_siminterface ();
  // Install callback function to handle anything that occurs before the
  // simulation begins.  This is responsible for displaying any error
  // dialogs during bochsrc and command line processing.
  SIM->set_notify_callback (&MyApp::DefaultCallback, this);
  MyFrame *frame = new MyFrame( "Bochs x86 Emulator", wxPoint(50,50), wxSize(450,340), wxMINIMIZE_BOX | wxSYSTEM_MENU | wxCAPTION );
  theFrame = frame;  // hack alert
  frame->Show( TRUE );
  SetTopWindow( frame );
  wxTheClipboard->UsePrimarySelection (true);
  // if quickstart is enabled, kick off the simulation
  if (SIM->get_param_enum(BXP_BOCHS_START)->get () == BX_QUICK_START) {
    wxCommandEvent unusedEvent;
    frame->OnStartSim (unusedEvent);
  }
  return TRUE;
}

int MyApp::OnExit ()
{
  return 0;
}

// these are only called when the simthread is not running.
BxEvent *
MyApp::DefaultCallback (void *thisptr, BxEvent *event)
{
  wxLogDebug ("DefaultCallback: event type %d", event->type);
  event->retcode = -1;  // default return code
  switch (event->type)
  {
    case BX_ASYNC_EVT_LOG_MSG:
    case BX_SYNC_EVT_LOG_ASK: {
      wxLogDebug ("DefaultCallback: log ask event");
      wxString text;
      text.Printf ("Error: %s", event->u.logmsg.msg);
      if (wxBochsClosing) {
        // gui closing down, do something simple and nongraphical.
        fprintf (stderr, "%s\n", text.c_str ());
      } else {
        wxMessageBox (text, "Error", wxOK | wxICON_ERROR );
        // maybe I can make OnLogMsg display something that looks appropriate.
        // theFrame->OnLogMsg (event);
      }
      event->retcode = BX_LOG_ASK_CHOICE_DIE;
      // There is only one thread at this point.  if I choose DIE here, it will
      // call fatal() and kill the whole app.
      break;
    }
    case BX_SYNC_EVT_TICK:
      if (wxBochsClosing) 
        event->retcode = -1;
      break;
    case BX_ASYNC_EVT_REFRESH:
    case BX_ASYNC_EVT_DBG_MSG:
      break;  // ignore
    case BX_SYNC_EVT_ASK_PARAM:
    case BX_SYNC_EVT_GET_DBG_COMMAND:
      break;  // ignore
    default:
      wxLogDebug ("DefaultCallback: unknown event type %d", event->type);
  }
  if (BX_EVT_IS_ASYNC(event->type)) {
    delete event;
    event = NULL;
  }
  return event;
}

//////////////////////////////////////////////////////////////////////
// MyFrame: the top level frame for the Bochs application
//////////////////////////////////////////////////////////////////////

BEGIN_EVENT_TABLE(MyFrame, wxFrame)
  EVT_MENU(ID_Config_New, MyFrame::OnConfigNew)
  EVT_MENU(ID_Config_Read, MyFrame::OnConfigRead)
  EVT_MENU(ID_Config_Save, MyFrame::OnConfigSave)
  EVT_MENU(ID_Quit, MyFrame::OnQuit)
  EVT_MENU(ID_Help_About, MyFrame::OnAbout)
  EVT_MENU(ID_Simulate_Start, MyFrame::OnStartSim)
  EVT_MENU(ID_Simulate_PauseResume, MyFrame::OnPauseResumeSim)
  EVT_MENU(ID_Simulate_Stop, MyFrame::OnKillSim)
  EVT_MENU(ID_Sim2CI_Event, MyFrame::OnSim2CIEvent)
  EVT_MENU(ID_Edit_ATA0, MyFrame::OnEditATA)
  EVT_MENU(ID_Edit_ATA1, MyFrame::OnEditATA)
  EVT_MENU(ID_Edit_ATA2, MyFrame::OnEditATA)
  EVT_MENU(ID_Edit_ATA3, MyFrame::OnEditATA)
  EVT_MENU(ID_Edit_Boot, MyFrame::OnEditBoot)
  EVT_MENU(ID_Edit_Memory, MyFrame::OnEditMemory)
  EVT_MENU(ID_Edit_Sound, MyFrame::OnEditSound)
  EVT_MENU(ID_Edit_Timing, MyFrame::OnEditTiming)
  EVT_MENU(ID_Edit_Network, MyFrame::OnEditNet)
  EVT_MENU(ID_Edit_Keyboard, MyFrame::OnEditKeyboard)
  EVT_MENU(ID_Edit_Serial_Parallel, MyFrame::OnEditSerialParallel)
  EVT_MENU(ID_Edit_LoadHack, MyFrame::OnEditLoadHack)
  EVT_MENU(ID_Edit_Other, MyFrame::OnEditOther)
  EVT_MENU(ID_Log_Prefs, MyFrame::OnLogPrefs)
  EVT_MENU(ID_Log_PrefsDevice, MyFrame::OnLogPrefsDevice)
  EVT_MENU(ID_Debug_ShowCpu, MyFrame::OnShowCpu)
  EVT_MENU(ID_Debug_ShowKeyboard, MyFrame::OnShowKeyboard)
#if BX_DEBUGGER
  EVT_MENU(ID_Debug_Console, MyFrame::OnDebugLog)
#endif
  // toolbar events
  EVT_TOOL(ID_Edit_FD_0, MyFrame::OnToolbarClick)
  EVT_TOOL(ID_Edit_FD_1, MyFrame::OnToolbarClick)
  EVT_TOOL(ID_Edit_Cdrom, MyFrame::OnToolbarClick)
  EVT_TOOL(ID_Toolbar_Reset, MyFrame::OnToolbarClick)
  EVT_TOOL(ID_Toolbar_Power, MyFrame::OnToolbarClick)
  EVT_TOOL(ID_Toolbar_Copy, MyFrame::OnToolbarClick)
  EVT_TOOL(ID_Toolbar_Paste, MyFrame::OnToolbarClick)
  EVT_TOOL(ID_Toolbar_Snapshot, MyFrame::OnToolbarClick)
  EVT_TOOL(ID_Toolbar_Config, MyFrame::OnToolbarClick)
  EVT_TOOL(ID_Toolbar_Mouse_en, MyFrame::OnToolbarClick)
  EVT_TOOL(ID_Toolbar_User, MyFrame::OnToolbarClick)
END_EVENT_TABLE()

//////////////////////////////////////////////////////////////////
// Menu layout (approximate)
//
// The actual menus will be changing so this probably isn't up
// to date, but having it in text form was useful in planning.
//////////////////////////////////////////////////////////////////
// - File
//   +----------------------+
//   | New Configuration    |
//   | Read Configuration   |
//   | Save Configuration   |
//   +----------------------+
//   | Quit                 |
//   +----------------------+
// - Edit
//   +----------------------+
//   | Floppy Disk 0...     |
//   | Floppy Disk 1...     |
//   | Hard Disk 0...       |
//   | Hard Disk 1...       |
//   | Cdrom...             |
//   | Boot...              |
//   | VGA...               |
//   | Memory...            |
//   | Sound...             |
//   | Networking...        |
//   | Keyboard...          |
//   | Other...             |
//   +----------------------+
// - Simulate
//   +----------------------+
//   | Start                |
//   | Pause/Resume         |
//   | Stop                 |
//   +----------------------|
// - Debug
//   +----------------------|
//   | Show CPU             |
//   | Show Memory          |
//   | ? what else ?        |
//   +----------------------|
// - Event Log
//   +----------------------+
//   | View                 |
//   | Preferences...       |
//   | By Device...         |
//   +----------------------+
// - Help
//   +----------------------+
//   | About Bochs...       |
//   +----------------------+
//////////////////////////////////////////////////////////////////

MyFrame::MyFrame(const wxString& title, const wxPoint& pos, const wxSize& size, const long style)
: wxFrame((wxFrame *)NULL, -1, title, pos, size, style)
{
  SetIcon(wxICON(icon_bochs));

  // init variables
  sim_thread = NULL;
  start_bochs_times = 0;
  showCpu = NULL;
  showKbd = NULL;
  debugCommand = NULL;
  debugCommandEvent = NULL;

  // set up the gui
  menuConfiguration = new wxMenu;
  menuConfiguration->Append( ID_Config_New, "&New Configuration" );
  menuConfiguration->Append( ID_Config_Read, "&Read Configuration" );
  menuConfiguration->Append( ID_Config_Save, "&Save Configuration" );
  menuConfiguration->AppendSeparator ();
  menuConfiguration->Append (ID_Quit, "&Quit");

  menuEdit = new wxMenu;
  menuEdit->Append( ID_Edit_FD_0, "Floppy Disk &0..." );
  menuEdit->Append( ID_Edit_FD_1, "Floppy Disk &1..." );
  menuEdit->Append( ID_Edit_ATA0, "ATA Channel 0..." );
  menuEdit->Append( ID_Edit_ATA1, "ATA Channel 1..." );
  menuEdit->Append( ID_Edit_ATA2, "ATA Channel 2..." );
  menuEdit->Append( ID_Edit_ATA3, "ATA Channel 3..." );
  menuEdit->Append( ID_Edit_Boot, "&Boot..." );
  menuEdit->Append( ID_Edit_Memory, "&Memory..." );
  menuEdit->Append( ID_Edit_Sound, "S&ound..." );
  menuEdit->Append( ID_Edit_Timing, "&Timing..." );
  menuEdit->Append( ID_Edit_Network, "&Network..." );
  menuEdit->Append( ID_Edit_Keyboard, "&Keyboard..." );
  menuEdit->Append( ID_Edit_Serial_Parallel, "&Serial/Parallel..." );
  menuEdit->Append( ID_Edit_LoadHack, "&Loader Hack..." );
  menuEdit->Append( ID_Edit_Other, "&Other..." );

  menuSimulate = new wxMenu;
  menuSimulate->Append( ID_Simulate_Start, "&Start...");
  menuSimulate->Append( ID_Simulate_PauseResume, "&Pause...");
  menuSimulate->Append( ID_Simulate_Stop, "S&top...");
  menuSimulate->AppendSeparator ();
  menuSimulate->Enable (ID_Simulate_PauseResume, FALSE);
  menuSimulate->Enable (ID_Simulate_Stop, FALSE);

  menuDebug = new wxMenu;
  menuDebug->Append (ID_Debug_ShowCpu, "Show &CPU");
  menuDebug->Append (ID_Debug_ShowKeyboard, "Show &Keyboard");
#if BX_DEBUGGER
  menuDebug->Append (ID_Debug_Console, "Debug Console");
#endif
  menuDebug->Append (ID_Debug_ShowMemory, "Show &memory");

  menuLog = new wxMenu;
  menuLog->Append (ID_Log_View, "&View");
  menuLog->Append (ID_Log_Prefs, "&Preferences...");
  menuLog->Append (ID_Log_PrefsDevice, "By &Device...");

  menuHelp = new wxMenu;
  menuHelp->Append( ID_Help_About, "&About..." );

  wxMenuBar *menuBar = new wxMenuBar;
  menuBar->Append( menuConfiguration, "&File" );
  menuBar->Append( menuEdit, "&Edit" );
  menuBar->Append( menuSimulate, "&Simulate" );
  menuBar->Append( menuDebug, "&Debug" );
  menuBar->Append( menuLog, "&Log" );
  menuBar->Append( menuHelp, "&Help" );
  SetMenuBar( menuBar );

  // disable things that don't work yet
  menuDebug->Enable (ID_Debug_ShowMemory, FALSE);  // not implemented
  menuLog->Enable (ID_Log_View, FALSE);  // not implemented

  CreateStatusBar();

  CreateToolBar(wxNO_BORDER|wxHORIZONTAL|wxTB_FLAT);
  wxToolBar *tb = GetToolBar();
  tb->SetToolBitmapSize(wxSize(32, 32));

#define BX_ADD_TOOL(id, xpm_name, tooltip) do { \
    tb->AddTool(id, wxBitmap(xpm_name), tooltip); \
  } while (0)

  BX_ADD_TOOL(ID_Edit_FD_0, floppya_xpm, "Change Floppy A");
  BX_ADD_TOOL(ID_Edit_FD_1, floppyb_xpm, "Change Floppy B");
  BX_ADD_TOOL(ID_Edit_Cdrom, cdromd_xpm, "Change CDROM");
  BX_ADD_TOOL(ID_Toolbar_Reset, reset_xpm, "Reset the system");
  BX_ADD_TOOL(ID_Toolbar_Power, power_xpm, "Turn power on/off");

  BX_ADD_TOOL(ID_Toolbar_Copy, copy_xpm, "Copy to clipboard");
  BX_ADD_TOOL(ID_Toolbar_Paste, paste_xpm, "Paste from clipboard");
  BX_ADD_TOOL(ID_Toolbar_Snapshot, snapshot_xpm, "Save screen snapshot");
  // Omit config button because the whole wxWindows interface is like
  // one really big config button.
  //BX_ADD_TOOL(ID_Toolbar_Config, configbutton_xpm, "Runtime Configuration");
  BX_ADD_TOOL(ID_Toolbar_Mouse_en, mouse_xpm, "Enable/disable mouse capture\nThere are also two shortcuts for this: F12 and the middle mouse button.");
  BX_ADD_TOOL(ID_Toolbar_User, userbutton_xpm, "Keyboard shortcut");

  tb->Realize();

  // create a MyPanel that covers the whole frame
  panel = new MyPanel (this, -1);
  panel->SetBackgroundColour (wxColour (0,0,0));
  panel->SetFocus ();
  wxGridSizer *sz = new wxGridSizer (1, 1);
  sz->Add (panel, 0, wxGROW);
  SetAutoLayout (TRUE);
  SetSizer (sz);

#if BX_DEBUGGER
  // create the debug log dialog box immediately so that we can write output
  // to it.
  showDebugLog = new DebugLogDialog (this, -1);
  showDebugLog->Init ();
#endif
}

MyFrame::~MyFrame ()
{
  delete panel;
  wxLogDebug ("MyFrame destructor");
  theFrame = NULL;
}

void MyFrame::OnConfigNew(wxCommandEvent& WXUNUSED(event))
{
  int answer = wxMessageBox ("This will reset all settings back to their default values.\nAre you sure you want to do this?",
    "Are you sure?", wxYES_NO | wxCENTER, this);
  if (answer == wxYES) SIM->reset_all_param ();
}

void MyFrame::OnConfigRead(wxCommandEvent& WXUNUSED(event))
{
  char *bochsrc;
  long style = wxOPEN;
  wxFileDialog *fdialog = new wxFileDialog (this, "Read configuration", "", "", "*.*", style);
  if (fdialog->ShowModal() == wxID_OK) {
    bochsrc = (char *)fdialog->GetPath().c_str ();
    SIM->reset_all_param ();
    SIM->read_rc (bochsrc);
  }
  delete fdialog;
}

void MyFrame::OnConfigSave(wxCommandEvent& WXUNUSED(event))
{
  char *bochsrc;
  long style = wxSAVE | wxOVERWRITE_PROMPT;
  wxFileDialog *fdialog = new wxFileDialog (this, "Save configuration", "", "", "*.*", style);
  if (fdialog->ShowModal() == wxID_OK) {
    bochsrc = (char *)fdialog->GetPath().c_str ();
    SIM->write_rc (bochsrc, 1);
  }
  delete fdialog;
}

void MyFrame::OnEditBoot(wxCommandEvent& WXUNUSED(event))
{
#define MAX_BOOT_DEVICES 3
  int bootDevices = 0;
  wxString devices[MAX_BOOT_DEVICES];
  int dev_id[MAX_BOOT_DEVICES];
  bx_param_enum_c *floppy = SIM->get_param_enum (BXP_FLOPPYA_DEVTYPE);
  if (floppy->get () != BX_FLOPPY_NONE) {
    devices[bootDevices] = wxT("First floppy drive");
    dev_id[bootDevices++] = BX_BOOT_FLOPPYA;
  }
  bx_param_c *firsthd = SIM->get_first_hd ();
  if (firsthd != NULL) {
    devices[bootDevices] = wxT("First hard drive");
    dev_id[bootDevices++] = BX_BOOT_DISKC;
  }
  bx_param_c *firstcd = SIM->get_first_cdrom ();
  if (firstcd != NULL) {
    devices[bootDevices] = wxT("CD-ROM drive");
    dev_id[bootDevices++] = BX_BOOT_CDROM;
  }
  if (bootDevices == 0) {
    wxMessageBox( "All the possible boot devices are disabled right now!\nYou must enable the first floppy drive, a hard drive, or a CD-ROM.",
                  "None enabled", wxOK | wxICON_ERROR, this );
    return;
  }
  int which = wxGetSingleChoiceIndex ("Select the device to boot from", "Boot Device", bootDevices, devices, this);
  if (which<0) return;  // cancelled
  bx_param_enum_c *bootdevice = (bx_param_enum_c *) 
    SIM->get_param(BXP_BOOTDRIVE);
  bootdevice->set (which);
}

void MyFrame::OnEditMemory(wxCommandEvent& WXUNUSED(event))
{
  ConfigMemoryDialog dlg (this, -1);
  dlg.ShowModal ();
}

void MyFrame::OnEditSound(wxCommandEvent& WXUNUSED(event))
{
  ParamDialog dlg (this, -1);
  bx_list_c *list = (bx_list_c*) SIM->get_param (BXP_SB16);
  dlg.SetTitle (list->get_name ());
  dlg.AddParam (list);
  dlg.ShowModal ();
}

void MyFrame::OnEditTiming(wxCommandEvent& WXUNUSED(event))
{
  ParamDialog dlg (this, -1);
  dlg.AddParam (SIM->get_param (BXP_IPS));
  bx_list_c *list = (bx_list_c*) SIM->get_param (BXP_CLOCK);
  dlg.SetTitle (list->get_name ());
  dlg.AddParam (list);
  dlg.ShowModal ();
}

void MyFrame::OnEditNet(wxCommandEvent& WXUNUSED(event))
{
  ParamDialog dlg (this, -1);
  bx_list_c *list = (bx_list_c*) SIM->get_param (BXP_NE2K);
  dlg.SetTitle (list->get_name ());
  dlg.AddParam (list);
  dlg.ShowModal ();
}

void MyFrame::OnEditKeyboard(wxCommandEvent& WXUNUSED(event))
{
  ParamDialog dlg(this, -1);
  bx_list_c *list = (bx_list_c*) SIM->get_param (BXP_MENU_KEYBOARD);
  dlg.SetTitle (list->get_name ());
  dlg.AddParam (list);
  dlg.SetRuntimeFlag (sim_thread != NULL);
  dlg.ShowModal ();
}

void MyFrame::OnEditSerialParallel(wxCommandEvent& WXUNUSED(event))
{
  ParamDialog dlg(this, -1);
  bx_list_c *list = (bx_list_c*) SIM->get_param (BXP_MENU_SERIAL_PARALLEL);
  dlg.SetTitle (list->get_name ());
  dlg.AddParam (list);
  dlg.ShowModal ();
}

void MyFrame::OnEditLoadHack(wxCommandEvent& WXUNUSED(event))
{
  ParamDialog dlg(this, -1);
  bx_list_c *list = (bx_list_c*) SIM->get_param (BXP_LOAD32BITOS);
  dlg.SetTitle (list->get_name ());
  dlg.AddParam (list);
  dlg.ShowModal ();
}

void MyFrame::OnEditOther(wxCommandEvent& WXUNUSED(event))
{
  ParamDialog dlg(this, -1);
  bx_list_c *list = (bx_list_c*) SIM->get_param (BXP_MENU_MISC_2);
  dlg.SetTitle (list->get_name ());
  dlg.AddParam (list);
  dlg.SetRuntimeFlag (sim_thread != NULL);
  dlg.ShowModal ();
}

void MyFrame::OnLogPrefs(wxCommandEvent& WXUNUSED(event))
{
  // Ideally I would use the siminterface methods to fill in the fields
  // of the LogOptionsDialog, but in fact several things are hardcoded.
  // At least I can verify that the expected numbers are the same.
  wxASSERT (SIM->get_max_log_level() == LOG_OPTS_N_TYPES);
  LogOptionsDialog dlg (this, -1);
  bx_param_string_c *logfile = SIM->get_param_string (BXP_LOG_FILENAME);
  dlg.SetLogfile (wxString (logfile->getptr ()));
  bx_param_string_c *debuggerlogfile = SIM->get_param_string (BXP_DEBUGGER_LOG_FILENAME);
  dlg.SetDebuggerlogfile (wxString (debuggerlogfile->getptr ()));

  // The inital values of the dialog are complicated.  If the panic action
  // for all modules is "ask", then clearly the inital value in the dialog
  // for panic action should be "ask".  This informs the user what the 
  // previous value was, and if they click Ok it won't do any harm.  But if
  // some devices are set to "ask" and others are set to "report", then the
  // initial value should be "no change".  With "no change", clicking on Ok
  // will not destroy the settings for individual devices.  You would only
  // start to see "no change" if you've been messing around in the advanced
  // menu already.
  int level, nlevel = SIM->get_max_log_level();
  for (level=0; level<nlevel; level++) {
    int mod = 0;
    int first = SIM->get_log_action (mod, level);
    bool consensus = true;
    // now compare all others to first.  If all match, then use "first" as
    // the initial value.
    for (mod=1; mod<SIM->get_n_log_modules (); mod++) {
      if (first != SIM->get_log_action (mod, level)) {
        consensus = false;
        break;
      }
    }
    if (consensus)
      dlg.SetAction (level, first);
    else
      dlg.SetAction (level, LOG_OPTS_NO_CHANGE);
  }
  int n = dlg.ShowModal ();   // show the dialog!
  if (n == wxID_OK) {
    char buf[1024];
    safeWxStrcpy (buf, dlg.GetLogfile (), sizeof (buf));
    logfile->set (buf);
    safeWxStrcpy (buf, dlg.GetDebuggerlogfile (), sizeof (buf));
    debuggerlogfile->set (buf);
    for (level=0; level<nlevel; level++) {
      // ask the dialog what action the user chose for this type of event
      int action = dlg.GetAction (level);
      if (action != LOG_OPTS_NO_CHANGE) {
        // set new default
        SIM->set_default_log_action (level, action);
        // apply that action to all modules (devices)
        SIM->set_log_action (-1, level, action);
      }
    }
  }
}

void MyFrame::OnLogPrefsDevice(wxCommandEvent& WXUNUSED(event))
{
  wxASSERT (SIM->get_max_log_level() == ADVLOG_OPTS_N_TYPES);
  AdvancedLogOptionsDialog dlg (this, -1);
  dlg.ShowModal ();
}

// How is this going to work?
// The dialog box shows the value of CPU registers, which will be changing
// all the time.  What causes the dialog to reread the register value and
// display it?  Brainstorm:
// 1) The update could be controlled by a real-time timer.
// 2) It could be triggered by periodic BX_SYNC_EVT_TICK events.  
// 3) It could be triggered by changes in the actual value.  This is
//    good for values that rarely change, but horrible for values like
//    EIP that change constantly.
// 4) An update can be forced by explictly calling an update function.  For
//   example after a single-step you would want to force an update.  If you
//   interrupt the simulation, you want to force an update.  If you manually
//   change a parameter, you would force an update.
// When simulation is free running, #1 or #2 might make sense.  Try #2.
void MyFrame::OnShowCpu(wxCommandEvent& WXUNUSED(event))
{
  if (SIM->get_param (BXP_CPU_EAX) == NULL) {
    // if params not initialized yet, then give up
    wxMessageBox ("Cannot show the debugger window until the simulation has begun.",
                  "Sim not started", wxOK | wxICON_ERROR, this );
    return;
  }
  if (showCpu == NULL) {
    showCpu = new CpuRegistersDialog (this, -1);
#if BX_DEBUGGER
    showCpu->SetTitle ("Bochs Debugger");
#else
    showCpu->SetTitle ("CPU Registers");
#endif
    showCpu->Init ();
  } else {
    showCpu->CopyParamToGui ();
  }
  showCpu->Show (TRUE);
}

void MyFrame::OnShowKeyboard(wxCommandEvent& WXUNUSED(event))
{
  if (SIM->get_param (BXP_KBD_PARAMETERS) == NULL) {
    // if params not initialized yet, then give up
    wxMessageBox ("Cannot show the debugger window until the simulation has begun.",
                  "Sim not started", wxOK | wxICON_ERROR, this );
    return;
  }
  if (showKbd == NULL) {
    showKbd = new ParamDialog (this, -1);
    showKbd->SetTitle ("Keyboard State (incomplete, this is a demo)");
    showKbd->AddParam (SIM->get_param (BXP_KBD_PARAMETERS));
    showKbd->Init ();
  } else {
    showKbd->CopyParamToGui ();
  }
  showKbd->Show (TRUE);
}

#if BX_DEBUGGER
void MyFrame::OnDebugLog(wxCommandEvent& WXUNUSED(event))
{
  wxASSERT (showDebugLog != NULL);
  showDebugLog->CopyParamToGui ();
  showDebugLog->Show (TRUE);
}

void
MyFrame::DebugBreak ()
{
  if (debugCommand) {
    delete debugCommand;
    debugCommand = NULL;
  }
  wxASSERT (showDebugLog != NULL);
  showDebugLog->AppendCommand ("*** break ***");
  SIM->debug_break ();
}

void
MyFrame::DebugCommand (wxString cmd)
{
  char buf[1024];
  safeWxStrcpy (buf, cmd, sizeof(buf));
  DebugCommand (buf);
}

void
MyFrame::DebugCommand (const char *cmd)
{
  wxLogDebug ("debugger command: %s", cmd);
  wxASSERT (showDebugLog != NULL);
  showDebugLog->AppendCommand (cmd);
  if (debugCommand != NULL) {
    // one is already waiting
    wxLogDebug ("multiple debugger commands, discarding the earlier one");
    delete debugCommand;
    debugCommand = NULL;
  }
  int len = strlen(cmd);
  char *tmp = new char[len+1];
  strncpy (tmp, cmd, len+1);
  // if an event is waiting for us, fill it an send back to sim_thread.
  if (debugCommandEvent != NULL) {
    wxLogDebug ("sim_thread was waiting for this command '%s'", tmp);
    wxASSERT (debugCommandEvent->type == BX_SYNC_EVT_GET_DBG_COMMAND);
    debugCommandEvent->u.debugcmd.command = tmp;
    debugCommandEvent->retcode = 1;
    sim_thread->SendSyncResponse (debugCommandEvent);
    wxASSERT (debugCommand == NULL);
    debugCommandEvent = NULL;
  } else {
    // store this command in debugCommand for the future
    wxLogDebug ("storing debugger command '%s'", tmp);
    debugCommand = tmp;
  }
}
#endif

void MyFrame::OnQuit(wxCommandEvent& event)
{
  wxBochsClosing = true;
  if (!sim_thread) {
    // no simulation thread is running. Just close the window.
    Close( TRUE );
  } else {
    SIM->set_notify_callback (&MyApp::DefaultCallback, this);
    // ask the simulator to stop.  When it stops it will close this frame.
    SetStatusText ("Waiting for simulation to stop...");
    OnKillSim (event);
  }
}

void MyFrame::OnAbout(wxCommandEvent& WXUNUSED(event))
{
  wxString str;
  str.Printf ("Bochs x86 Emulator version %s (wxWindows port)", VER_STRING);
  wxMessageBox( str, "About Bochs", wxOK | wxICON_INFORMATION, this );
}

// update the menu items, status bar, etc.
void MyFrame::simStatusChanged (StatusChange change, bx_bool popupNotify) {
  switch (change) {
    case Start:  // running
      wxLogStatus ("Starting Bochs simulation");
      menuSimulate->Enable (ID_Simulate_Start, FALSE);
      menuSimulate->Enable (ID_Simulate_PauseResume, TRUE);
      menuSimulate->Enable (ID_Simulate_Stop, TRUE);
      menuSimulate->SetLabel (ID_Simulate_PauseResume, "&Pause");
      break;
    case Stop: // not running
      wxLogStatus ("Simulation stopped");
      menuSimulate->Enable (ID_Simulate_Start, TRUE);
      menuSimulate->Enable (ID_Simulate_PauseResume, FALSE);
      menuSimulate->Enable (ID_Simulate_Stop, FALSE);
      menuSimulate->SetLabel (ID_Simulate_PauseResume, "&Pause");
      // This should only be used if the simulation stops due to error.
      // Obviously if the user asked it to stop, they don't need to be told.
      if (popupNotify)
        wxMessageBox("Bochs simulation has stopped.", "Bochs Stopped", 
            wxOK | wxICON_INFORMATION, this);
      break;
    case Pause: // pause
      wxLogStatus ("Pausing simulation");
      menuSimulate->SetLabel (ID_Simulate_PauseResume, "&Resume");
      break;
    case Resume: // resume
      wxLogStatus ("Resuming simulation");
      menuSimulate->SetLabel (ID_Simulate_PauseResume, "&Pause");
      break;
  }
  bool canConfigure = (change == Stop);
  menuConfiguration->Enable (ID_Config_New, canConfigure);
  menuConfiguration->Enable (ID_Config_Read, canConfigure);
  // only enabled ATA channels with a cdrom connected are available at runtime
  for (unsigned i=0; i<4; i++) {
    if (!SIM->get_param_bool((bx_id)(BXP_ATA0_PRESENT+i))->get ()) {
      menuEdit->Enable (ID_Edit_ATA0+i, canConfigure);
    } else {
      if ( (SIM->get_param_num((bx_id)(BXP_ATA0_MASTER_TYPE+i*2))->get () != BX_ATA_DEVICE_CDROM) &&
           (SIM->get_param_num((bx_id)(BXP_ATA0_SLAVE_TYPE+i*2))->get () != BX_ATA_DEVICE_CDROM) ) {
        menuEdit->Enable (ID_Edit_ATA0+i, canConfigure);
      }
    }
  }
  menuEdit->Enable( ID_Edit_Boot, canConfigure);
  menuEdit->Enable( ID_Edit_Memory, canConfigure);
  menuEdit->Enable( ID_Edit_Sound, canConfigure);
  menuEdit->Enable( ID_Edit_Timing, canConfigure);
  menuEdit->Enable( ID_Edit_Network, canConfigure);
  menuEdit->Enable( ID_Edit_Serial_Parallel, canConfigure);
  menuEdit->Enable( ID_Edit_LoadHack, canConfigure);
  // during simulation, certain menu options like the floppy disk
  // can be modified under some circumstances.  A floppy drive can
  // only be edited if it was enabled at boot time.
  bx_param_c *param;
  param = SIM->get_param(BXP_FLOPPYA);
  menuEdit->Enable (ID_Edit_FD_0, canConfigure || param->get_enabled ());
  param = SIM->get_param(BXP_FLOPPYB);
  menuEdit->Enable (ID_Edit_FD_1, canConfigure || param->get_enabled ());
  /*
  // this menu item removed, since you can configure the cdrom from the
  // ATA controller menu items instead.
  param = SIM->get_first_cdrom ();
  menuEdit->Enable (ID_Edit_Cdrom, canConfigure || (param&&param->get_enabled ()));
  */
}

void MyFrame::OnStartSim(wxCommandEvent& event)
{
  wxCriticalSectionLocker lock(sim_thread_lock);
  if (sim_thread != NULL) {
        wxMessageBox (
          "Can't start Bochs simulator, because it is already running",
          "Already Running", wxOK | wxICON_ERROR, this);
        return;
  }
  // check that display library is set to wx.  If not, give a warning and
  // change it to wx.  It is technically possible to use other vga libraries
  // with the wx config interface, but there are still some significant
  // problems.
  bx_param_enum_c *gui_param = SIM->get_param_enum(BXP_SEL_DISPLAY_LIBRARY);
  char *gui_name = gui_param->get_choice (gui_param->get ());
  if (strcmp (gui_name, "wx") != 0) {
    wxMessageBox (
    "The display library was not set to wxWindows.  When you use the\n"
    "wxWindows configuration interface, you must also select the wxWindows\n"
    "display library.  I will change it to 'wx' now.",
    "display library error", wxOK | wxICON_WARNING, this);
    if (!gui_param->set_by_name ("wx")) {
      wxASSERT (0 && "Could not set display library setting to 'wx");
    }
  }
  // give warning about restarting the simulation
  start_bochs_times++;
  if (start_bochs_times>1) {
        wxMessageBox (
        "You have already started the simulator once this session. Due to memory leaks and bugs in init code, you may get unstable behavior.",
        "2nd time warning", wxOK | wxICON_WARNING, this);
  }
  num_events = 0;  // clear the queue of events for bochs to handle
  sim_thread = new SimThread (this);
  sim_thread->Create ();
  sim_thread->Run ();                                                        
  wxLogDebug ("Simulator thread has started.");
  // set up callback for events from simulator thread
  SIM->set_notify_callback (&SimThread::SiminterfaceCallback, sim_thread);
  simStatusChanged (Start);
}

void MyFrame::OnPauseResumeSim(wxCommandEvent& WXUNUSED(event))
{
  wxCriticalSectionLocker lock(sim_thread_lock);
  if (sim_thread) {
    if (sim_thread->IsPaused ()) {
      simStatusChanged (Resume);
          sim_thread->Resume ();
        } else {
      simStatusChanged (Pause);
          sim_thread->Pause ();
        }
  }
}

void MyFrame::OnKillSim(wxCommandEvent& WXUNUSED(event))
{
  // DON'T use a critical section here.  Delete implicitly calls
  // OnSimThreadExit, which also tries to lock sim_thread_lock.
  // If we grab the lock at this level, deadlock results.
  wxLogDebug ("OnKillSim()");
#if BX_DEBUGGER
  // the sim_thread may be waiting for a debugger command.  If so, send
  // it a "quit"
  DebugCommand ("quit");
#endif
  if (sim_thread) {
    sim_thread->Delete ();
    // Next time the simulator reaches bx_real_sim_c::periodic() it
    // will quit.  This is better than killing the thread because it
    // gives it a chance to clean up after itself.
  }
}

void
MyFrame::OnSimThreadExit () {
  wxCriticalSectionLocker lock (sim_thread_lock);
  sim_thread = NULL; 
}

int 
MyFrame::HandleAskParamString (bx_param_string_c *param)
{
  wxLogDebug ("HandleAskParamString start");
  bx_param_num_c *opt = param->get_options ();
  wxASSERT (opt != NULL);
  int n_opt = opt->get ();
  char *msg = param->get_name ();
  char *newval = NULL;
  wxDialog *dialog = NULL;
  if (n_opt & param->IS_FILENAME) {
    // use file open dialog
        long style = 
          (n_opt & param->SAVE_FILE_DIALOG) ? wxSAVE|wxOVERWRITE_PROMPT : wxOPEN;
        wxLogDebug ("HandleAskParamString: create dialog");
        wxFileDialog *fdialog = new wxFileDialog (this, msg, "", wxString(param->getptr ()), "*.*", style);
        wxLogDebug ("HandleAskParamString: before showmodal");
        if (fdialog->ShowModal() == wxID_OK)
          newval = (char *)fdialog->GetPath().c_str ();
        wxLogDebug ("HandleAskParamString: after showmodal");
        dialog = fdialog; // so I can delete it
  } else {
    // use simple string dialog
        long style = wxOK|wxCANCEL;
        wxTextEntryDialog *tdialog = new wxTextEntryDialog (this, msg, "Enter new value", wxString(param->getptr ()), style);
        if (tdialog->ShowModal() == wxID_OK)
          newval = (char *)tdialog->GetValue().c_str ();
        dialog = tdialog; // so I can delete it
  }
  // newval points to memory inside the dialog.  As soon as dialog is deleted,
  // newval points to junk.  So be sure to copy the text out before deleting
  // it!
  if (newval && strlen(newval)>0) {
        // change floppy path to this value.
        wxLogDebug ("Setting param %s to '%s'", param->get_name (), newval);
        param->set (newval);
        delete dialog;
        return 1;
  }
  delete dialog;
  return -1;
}

// This is called when the simulator needs to ask the user to choose
// a value or setting.  For example, when the user indicates that he wants
// to change the floppy disk image for drive A, an ask-param event is created
// with the parameter id set to BXP_FLOPPYA_PATH.  The simulator blocks until
// the gui has displayed a dialog and received a selection from the user.
// In the current implemention, the GUI will look up the parameter's 
// data structure using SIM->get_param() and then call the set method on the
// parameter to change the param.  The return value only needs to return
// success or failure (failure = cancelled, or not implemented).
// Returns 1 if the user chose a value and the param was modified.
// Returns 0 if the user cancelled.
// Returns -1 if the gui doesn't know how to ask for that param.
int 
MyFrame::HandleAskParam (BxEvent *event)
{
  wxASSERT (event->type == BX_SYNC_EVT_ASK_PARAM);

  bx_param_c *param = event->u.param.param;
  Raise ();  // bring window to front so that you will see the dialog
  switch (param->get_type ())
  {
  case BXT_PARAM_STRING:
    return HandleAskParamString ((bx_param_string_c *)param);
  default:
    {
          wxString msg;
          msg.Printf ("ask param for parameter type %d is not implemented in wxWindows",
                      param->get_type ());
          wxMessageBox( msg, "not implemented", wxOK | wxICON_ERROR, this );
          return -1;
        }
  }
#if 0
  switch (param) {
  case BXP_FLOPPYA_PATH:
  case BXP_FLOPPYB_PATH:
  case BXP_DISKC_PATH:
  case BXP_DISKD_PATH:
  case BXP_CDROM_PATH:
        {
          Raise();  // bring window to front so dialog shows
          char *msg;
          if (param==BXP_FLOPPYA_PATH || param==BXP_FLOPPYB_PATH)
            msg = "Choose new floppy disk image file";
      else if (param==BXP_DISKC_PATH || param==BXP_DISKD_PATH)
            msg = "Choose new hard disk image file";
      else if (param==BXP_CDROM_PATH)
            msg = "Choose new CDROM image file";
          else
            msg = "Choose new image file";
          wxFileDialog dialog(this, msg, "", "", "*.*", 0);
          int ret = dialog.ShowModal();
          if (ret == wxID_OK)
          {
            char *newpath = (char *)dialog.GetPath().c_str ();
            if (newpath && strlen(newpath)>0) {
              // change floppy path to this value.
              bx_param_string_c *Opath = SIM->get_param_string (param);
              assert (Opath != NULL);
              wxLogDebug ("Setting floppy %c path to '%s'", 
                    param == BXP_FLOPPYA_PATH ? 'A' : 'B',
                    newpath);
              Opath->set (newpath);
              return 1;
            }
          }
          return 0;
        }
  default:
        wxLogError ("HandleAskParam: parameter %d, not implemented", event->u.param.id);
  }
#endif
  return -1;  // could not display
}

// This is called from the wxWindows GUI thread, when a Sim2CI event
// is found.  (It got there via wxPostEvent in SiminterfaceCallback2, which is
// executed in the simulator Thread.)
void 
MyFrame::OnSim2CIEvent (wxCommandEvent& event)
{
  IFDBG_EVENT (wxLogDebug ("received a bochs event in the GUI thread"));
  BxEvent *be = (BxEvent *) event.GetEventObject ();
  IFDBG_EVENT (wxLogDebug ("event type = %d", (int) be->type));
  // all cases should return.  sync event handlers MUST send back a 
  // response.  async event handlers MUST delete the event.
  switch (be->type) {
  case BX_ASYNC_EVT_REFRESH:
    RefreshDialogs ();
    break;
  case BX_SYNC_EVT_ASK_PARAM:
    wxLogDebug ("before HandleAskParam");
    be->retcode = HandleAskParam (be);
    wxLogDebug ("after HandleAskParam");
    // return a copy of the event back to the sender.
    sim_thread->SendSyncResponse(be);
    wxLogDebug ("after SendSyncResponse");
    break;
#if BX_DEBUGGER
  case BX_ASYNC_EVT_DBG_MSG:
    showDebugLog->AppendText (be->u.logmsg.msg);
    // free the char* which was allocated in dbg_printf
    delete [] ((char*) be->u.logmsg.msg);
    break;
#endif
  case BX_SYNC_EVT_LOG_ASK:
  case BX_ASYNC_EVT_LOG_MSG:
    OnLogMsg (be);
    break;
  case BX_SYNC_EVT_GET_DBG_COMMAND:
    wxLogDebug ("BX_SYNC_EVT_GET_DBG_COMMAND received");
    if (debugCommand == NULL) {
      // no debugger command is ready to send, so don't send a response yet.
      // When a command is issued, MyFrame::DebugCommand will fill in the
      // event and call SendSyncResponse() so that the simulation thread can
      // continue.
      debugCommandEvent = be;
      //
      if (showCpu == NULL || !showCpu->IsShowing ()) {
        wxCommandEvent unused;
        OnShowCpu (unused);
      }
    } else {
      // a debugger command is waiting for us!
      wxLogDebug ("sending debugger command '%s' that was waiting", debugCommand);
      be->u.debugcmd.command = debugCommand;
      debugCommand = NULL;  // ready for the next one
      debugCommandEvent = NULL;
      be->retcode = 1;
      sim_thread->SendSyncResponse (be);
    }
    break;
  default:
    wxLogDebug ("OnSim2CIEvent: event type %d ignored", (int)be->type);
    if (!BX_EVT_IS_ASYNC(be->type)) {
      // if it's a synchronous event, and we fail to send back a response,
      // the sim thread will wait forever.  So send something!
      sim_thread->SendSyncResponse(be);
    }
    break;
  }
  if (BX_EVT_IS_ASYNC(be->type))
    delete be;
}

void MyFrame::OnLogMsg (BxEvent *be) {
  wxLogDebug ("log msg: level=%d, prefix='%s', msg='%s'",
      be->u.logmsg.level,
      be->u.logmsg.prefix,
      be->u.logmsg.msg);
  if (be->type == BX_ASYNC_EVT_LOG_MSG)
    return;  // we don't have any place to display log messages
  else
    wxASSERT (be->type == BX_SYNC_EVT_LOG_ASK);
  wxString levelName (SIM->get_log_level_name (be->u.logmsg.level));
  LogMsgAskDialog dlg (this, -1, levelName);  // panic, error, etc.
#if !BX_DEBUGGER
  dlg.EnableButton (dlg.DEBUG, FALSE);
#endif
  dlg.SetContext (be->u.logmsg.prefix);
  dlg.SetMessage (be->u.logmsg.msg);
  int n = dlg.ShowModal ();
  // turn the return value into the constant that logfunctions::ask is
  // expecting.  0=continue, 1=continue but ignore future messages from this
  // device, 2=die, 3=dump core, 4=debugger.
  if (n==BX_LOG_ASK_CHOICE_CONTINUE) {
    if (dlg.GetDontAsk ()) n = BX_LOG_ASK_CHOICE_CONTINUE_ALWAYS; 
  }
  be->retcode = n;
  wxLogDebug ("you chose %d", n);
  // This can be called from two different contexts:
  // 1) before sim_thread starts, the default application callback can
  //    call OnLogMsg to display messages.
  // 2) after the sim_thread starts, the sim_thread callback can call
  //    OnLogMsg to display messages
  if (sim_thread)  
    sim_thread->SendSyncResponse (be);  // only for case #2
}

bool
MyFrame::editFloppyValidate (FloppyConfigDialog *dialog)
{
  // haven't done anything with this 'feature'
  return true;
}

void MyFrame::editFloppyConfig (int drive)
{
  FloppyConfigDialog dlg (this, -1);
  dlg.SetDriveName (wxString (drive==0? BX_FLOPPY0_NAME : BX_FLOPPY1_NAME));
  dlg.SetCapacityChoices (n_floppy_type_names, floppy_type_names);
  bx_list_c *list = (bx_list_c*) SIM->get_param ((drive==0)? BXP_FLOPPYA : BXP_FLOPPYB);
  if (!list) { wxLogError ("floppy object param is null"); return; }
  bx_param_filename_c *fname = (bx_param_filename_c*) list->get(0);
  bx_param_enum_c *disktype = (bx_param_enum_c *) list->get(1);
  bx_param_enum_c *status = (bx_param_enum_c *) list->get(2);
  if (fname->get_type () != BXT_PARAM_STRING
      || disktype->get_type () != BXT_PARAM_ENUM 
      || status->get_type() != BXT_PARAM_ENUM) {
    wxLogError ("floppy params have wrong type");
    return;
  }
  dlg.AddRadio ("Not Present", "");
  dlg.AddRadio ("Ejected", "none");
#if defined(__linux__)
  dlg.AddRadio ("Physical floppy drive /dev/fd0", "/dev/fd0");
  dlg.AddRadio ("Physical floppy drive /dev/fd1", "/dev/fd1");
#elif defined(WIN32)
  dlg.AddRadio ("Physical floppy drive A:", "A:");
  dlg.AddRadio ("Physical floppy drive B:", "B:");
#else
  // add your favorite operating system here
#endif
  dlg.SetCapacity (disktype->get () - disktype->get_min ());
  dlg.SetFilename (fname->getptr ());
  dlg.SetValidateFunc (editFloppyValidate);
  if (disktype->get() == BX_FLOPPY_NONE) {
    dlg.SetRadio (0);
  } else if (!strcmp ("none", fname->getptr ())) {
    dlg.SetRadio (1);
  } else {
    // otherwise the SetFilename() should have done the right thing.
  }
  int n = dlg.ShowModal ();
  wxLogMessage ("floppy config returned %d", n);
  if (n==wxID_OK) {
    char filename[1024];
    wxString fn (dlg.GetFilename ());
    strncpy (filename, fn.c_str (), sizeof(filename));
    wxLogMessage ("filename is '%s'", filename);
    wxLogMessage ("capacity = %d (%s)", dlg.GetCapacity(), floppy_type_names[dlg.GetCapacity ()]);
    fname->set (filename);
    disktype->set (disktype->get_min () + dlg.GetCapacity ());
    if (dlg.GetRadio () == 0)
      disktype->set (BX_FLOPPY_NONE);
  }
}

void MyFrame::editFirstCdrom ()
{
  bx_param_c *firstcd = SIM->get_first_cdrom ();
  if (!firstcd) {
    wxMessageBox ("No CDROM drive is enabled.  Use Edit:ATA to set one up.",
                  "No CDROM", wxOK | wxICON_ERROR, this );
    return;
  }
  ParamDialog dlg (this, -1);
  dlg.SetTitle ("Configure CDROM");
  dlg.AddParam (firstcd);
  dlg.SetRuntimeFlag (sim_thread != NULL);
  dlg.ShowModal ();
}

void MyFrame::OnEditATA (wxCommandEvent& event)
{
  int id = event.GetId ();
  int channel = id - ID_Edit_ATA0;
  ParamDialog dlg (this, -1);
  wxString str;
  str.Printf ("Configure ATA%d", channel);
  dlg.SetTitle (str);
  dlg.SetRuntimeFlag (sim_thread != NULL);
  dlg.AddParam (SIM->get_param ((bx_id)(BXP_ATA0_MENU+channel)));
  dlg.ShowModal ();
}

void MyFrame::OnToolbarClick(wxCommandEvent& event)
{
  wxLogDebug ("clicked toolbar thingy");
  bx_toolbar_buttons which = BX_TOOLBAR_UNDEFINED;
  int id = event.GetId ();
  switch (id) {
    case ID_Toolbar_Power:which = BX_TOOLBAR_POWER; break;
    case ID_Toolbar_Reset: which = BX_TOOLBAR_RESET; break;
    case ID_Edit_FD_0: 
      // floppy config dialog box
      editFloppyConfig (0);
      break;
    case ID_Edit_FD_1: 
      // floppy config dialog box
      editFloppyConfig (1);
      break;
    case ID_Edit_Cdrom:
      // cdrom config dialog box (first cd only)
      editFirstCdrom ();
      break;
    case ID_Toolbar_Copy: which = BX_TOOLBAR_COPY; break;
    case ID_Toolbar_Paste: which = BX_TOOLBAR_PASTE; break;
    case ID_Toolbar_Snapshot: which = BX_TOOLBAR_SNAPSHOT; break;
    case ID_Toolbar_Config: which = BX_TOOLBAR_CONFIG; break;
    case ID_Toolbar_Mouse_en: which = BX_TOOLBAR_MOUSE_EN; break;
    case ID_Toolbar_User: which = BX_TOOLBAR_USER; break;
    default:
      wxLogError ("unknown toolbar id %d", id);
  }
  if (num_events < MAX_EVENTS) {
    event_queue[num_events].type = BX_ASYNC_EVT_TOOLBAR;
    event_queue[num_events].u.toolbar.button = which;
    num_events++;
  }
}

// warning: This can be called from the simulator thread!!!
bool MyFrame::WantRefresh () {
  bool anyShowing = false;
  if (showCpu!=NULL && showCpu->IsShowing ()) anyShowing = true;
  if (showKbd!=NULL && showKbd->IsShowing ()) anyShowing = true;
  return anyShowing;
}

void MyFrame::RefreshDialogs () {
  if (showCpu!=NULL && showCpu->IsShowing ()) showCpu->CopyParamToGui ();
  if (showKbd!=NULL && showKbd->IsShowing ()) showKbd->CopyParamToGui ();
}

//////////////////////////////////////////////////////////////////////
// Simulation Thread
//////////////////////////////////////////////////////////////////////

void *
SimThread::Entry (void)
{
  // run all the rest of the Bochs simulator code.  This function will
  // run forever, unless a "kill_bochs_request" is issued.  The shutdown
  // procedure is as follows:
  //   - user selects "Kill Simulation" or GUI decides to kill bochs
  //   - GUI calls sim_thread->Delete ()
  //   - sim continues to run until the next time it reaches SIM->periodic().
  //   - SIM->periodic() sends a synchronous tick event to the GUI, which
  //     finally calls TestDestroy() and realizes it needs to stop.  It
  //     sets the sync event return code to -1.  SIM->periodic() notices
  //     the -1 and calls quit_sim, which longjumps to quit_context, which is
  //     right here in SimThread::Entry.
  //   - Entry() exits and the thread stops. Whew.
  wxLogDebug ("in SimThread, starting at bx_continue_after_config_interface");
  static jmp_buf context;  // this must not go out of scope. maybe static not needed
  if (setjmp (context) == 0) {
    SIM->set_quit_context (&context);
    SIM->begin_simulation (bx_startup_flags.argc, bx_startup_flags.argv);
    wxLogDebug ("in SimThread, SIM->begin_simulation() exited normally");
  } else {
    wxLogDebug ("in SimThread, SIM->begin_simulation() exited by longjmp");
  }
  SIM->set_quit_context (NULL);
  // it is possible that the whole interface has already been shut down.
  // If so, we must end immediately.
  // we're in the sim thread, so we must get a gui mutex before calling
  // wxwindows methods.
  wxLogDebug ("SimThread::Entry: get gui mutex");
  wxMutexGuiEnter();
  if (!wxBochsClosing) {
    wxLogDebug ("SimThread::Entry: sim thread ending.  call simStatusChanged");
    theFrame->simStatusChanged (theFrame->Stop, true);
  } else {
    wxLogMessage ("SimThread::Entry: the gui is waiting for sim to finish.  Now that it has finished, I will close the frame.");
    theFrame->Close (TRUE);
  }
  wxMutexGuiLeave();
  return NULL;
}

void
SimThread::OnExit ()
{
  // notify the MyFrame that the bochs thread has died.  I can't adjust
  // the sim_thread directly because it's private.
  frame->OnSimThreadExit ();
  // don't use this SimThread's callback function anymore.  Use the
  // application default callback.
  SIM->set_notify_callback (&MyApp::DefaultCallback, this);
}

// Event handler function for BxEvents coming from the simulator.
// This function is declared static so that I can get a usable
// function pointer for it.  The function pointer is passed to
// SIM->set_notify_callback so that the siminterface can call this
// function when it needs to contact the gui.  It will always be
// called with a pointer to the SimThread as the first argument, and
// it will be called from the simulator thread, not the GUI thread.
BxEvent *
SimThread::SiminterfaceCallback (void *thisptr, BxEvent *event)
{
  SimThread *me = (SimThread *)thisptr;
  // call the normal non-static method now that we know the this pointer.
  return me->SiminterfaceCallback2 (event);
}

// callback function for sim thread events.  This is called from
// the sim thread, not the GUI thread.  So any GUI actions must be
// thread safe.  Most events are handled by packaging up the event
// in a wxEvent of some kind, and posting it to the GUI thread for
// processing.
BxEvent *
SimThread::SiminterfaceCallback2 (BxEvent *event)
{
  //wxLogDebug ("SiminterfaceCallback with event type=%d", (int)event->type);
  event->retcode = 0;  // default return code
  int async = BX_EVT_IS_ASYNC(event->type);
  if (!async) {
    // for synchronous events, clear away any previous response.  There
        // can only be one synchronous event pending at a time.
    ClearSyncResponse ();
        event->retcode = -1;   // default to error
  }

  // tick event must be handled right here in the bochs thread.
  if (event->type == BX_SYNC_EVT_TICK) {
        if (TestDestroy ()) {
          // tell simulator to quit
          event->retcode = -1;
        } else {
          event->retcode = 0;
        }
        return event;
  }

  // prune refresh events if the frame is going to ignore them anyway
  if (event->type == BX_ASYNC_EVT_REFRESH && !theFrame->WantRefresh ()) {
    delete event;
    return NULL;
  }

  //encapsulate the bxevent in a wxwindows event
  wxCommandEvent wxevent (wxEVT_COMMAND_MENU_SELECTED, ID_Sim2CI_Event);
  wxevent.SetEventObject ((wxEvent *)event);
  if (isSimThread ()) {
    IFDBG_EVENT (wxLogDebug ("Sending an event to the window"));
    wxPostEvent (frame, wxevent);
    // if it is an asynchronous event, return immediately.  The event will be
    // freed by the recipient in the GUI thread.
    if (async) return NULL;
    wxLogDebug ("SiminterfaceCallback2: synchronous event; waiting for response");
    // now wait forever for the GUI to post a response.
    BxEvent *response = NULL;
    while (response == NULL) {
          response = GetSyncResponse ();
          if (!response) {
            //wxLogDebug ("no sync response yet, waiting");
            this->Sleep (20);
          }
          // don't get stuck here if the gui is trying to close.
          if (wxBochsClosing) {
            wxLogDebug ("breaking out of sync event wait because gui is closing");
            event->retcode = -1;
            return event;
          }
    }
    wxASSERT (response != NULL);
    return response;
  } else {
    wxLogDebug ("sim2ci event sent from the GUI thread. calling handler directly");
    theFrame->OnSim2CIEvent (wxevent);
    return event;
  }
}

void 
SimThread::ClearSyncResponse ()
{
  wxCriticalSectionLocker lock(sim2gui_mailbox_lock);
  if (sim2gui_mailbox != NULL) {
    wxLogDebug ("WARNING: ClearSyncResponse is throwing away an event that was previously in the mailbox");
  }
  sim2gui_mailbox = NULL;
}

void 
SimThread::SendSyncResponse (BxEvent *event)
{
  wxCriticalSectionLocker lock(sim2gui_mailbox_lock);
  if (sim2gui_mailbox != NULL) {
    wxLogDebug ("WARNING: SendSyncResponse is throwing away an event that was previously in the mailbox");
  }
  sim2gui_mailbox = event;
}

BxEvent *
SimThread::GetSyncResponse ()
{
  wxCriticalSectionLocker lock(sim2gui_mailbox_lock);
  BxEvent *event = sim2gui_mailbox;
  sim2gui_mailbox = NULL;
  return event;
}

///////////////////////////////////////////////////////////////////
// utility
///////////////////////////////////////////////////////////////////
void 
safeWxStrcpy (char *dest, wxString src, int destlen)
{
  wxString tmp (src);
  strncpy (dest, tmp.c_str (), destlen);
  dest[destlen-1] = 0;
}

#endif /* if BX_WITH_WX */
