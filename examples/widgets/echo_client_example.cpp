#include <SNFCore/Application.h>
#include <SNFNetwork/TcpSocket.h>
#include <SNFWidgets/ApplicationNode.h>
#include <SNFWidgets/Label.h>
#include <SNFWidgets/Layout.h>
#include <SNFWidgets/LineEdit.h>
#include <SNFWidgets/PushButton.h>
#include <SNFWidgets/RadioButton.h>
#include <SNFWidgets/SpinBox.h>
#include <SNFWidgets/Window.h>

int main(int argc, char** argv)
{
    snf::Application app(argc, argv);
    snf::TcpSocket sock;
    snf::widgets::ApplicationNode screenNode;  // The node that creates the 'context'
    screenNode.setTitle("SNF Echo client!!");

    // Set the window to the context
    snf::widgets::Window window("ImGui echo client", &screenNode);
    window.setInitialSize(400.0f, 200.0f);

    snf::widgets::VBoxLayout main_layout;  // our main layout for the window
    window.setLayout(&main_layout);

    // connect layout
    snf::widgets::HBoxLayout connect_layout;
    main_layout.addWidget(&connect_layout);  // Add our first row to the layout
    // connect layout - Widgets
    snf::widgets::LineEdit host_edit("Host");
    connect_layout.addWidget(&host_edit, 1);  // Max space for the line edit (see layout weights)

    snf::widgets::SpinBox port_edit("Port",
                                    3000,  // min value
                                    65535  // max value
    );
    connect_layout.addWidget(&port_edit, 0);  // min space for the spinbox
    // Button signal
    snf::widgets::PushButton connect_btn("Connect");
    connect_btn.clicked.connect([&]() {
        // Call when the button is clicked
        sock.connectToHost(host_edit.text(), port_edit.value());
    });
    connect_layout.addWidget(&connect_btn, 0);  // min space for the button

    // status_layout
    snf::widgets::HBoxLayout status_layout;
    main_layout.addWidget(&status_layout, 0);  // Add our second row to
    // status_layout - Widgets
    snf::widgets::Label status_label("Status: Disconnected");
    status_layout.addWidget(&status_label);  // Max space for the label

    // data_layout
    snf::widgets::HBoxLayout data_layout;
    main_layout.addWidget(&data_layout, 1);  // Add our third row to the layout
    // data_layout - Widgets
    snf::widgets::LineEdit data_edit("Data to send");
    data_layout.addWidget(&data_edit, 1);  // Max space for the line edit
    snf::widgets::PushButton send_btn("Send");
    send_btn.clicked.connect([&]() {
        // Call when the button is clicked
        sock.write(data_edit.text());
    });
    data_layout.addWidget(&send_btn, 0);  // Min space for the button
    // recv layout
    snf::widgets::HBoxLayout recv_layout;
    main_layout.addWidget(&recv_layout, 1);  // Add our fourth row to the layout
    // recv layout - Widgets
    snf::widgets::Label recv_label("Received: ");
    recv_layout.addWidget(&recv_label);  // Max space for the label
    snf::widgets::LineEdit recv_edit;
    recv_edit.setReadOnly(true);  // Make the line edit read-only
    recv_layout.addWidget(&recv_edit, 1);  // Max space for the line edit
    
    // Socket signals
    sock.connected.connect([&]() { status_label.setText("Status: Connected"); });
    sock.disconnected.connect([&]() { status_label.setText("Status: Disconnected"); });
    sock.readyRead.connect([&]() {
        // Call when there is data to read
        const std::string data = sock.readAll().toString();
        recv_edit.setText(data);
    });
    return app.run();
}
