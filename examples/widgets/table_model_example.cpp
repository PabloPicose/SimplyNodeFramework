#include <SNFCore/AbstractTableModel.h>
#include <SNFCore/Application.h>
#include <SNFWidgets/ApplicationNode.h>
#include <SNFWidgets/DataWidgetMapper.h>
#include <SNFWidgets/Label.h>
#include <SNFWidgets/Layout.h>
#include <SNFWidgets/LineEdit.h>
#include <SNFWidgets/PushButton.h>
#include <SNFWidgets/TableView.h>
#include <SNFWidgets/Window.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace wg = snf::widgets;

class PeopleModel final : public snf::AbstractTableModel
{
public:
    PeopleModel()
        : m_rows{
              {"Ada", "Lovelace", "Analytical Engine notes"},
              {"Grace", "Hopper", "Compiler and debugging tools"},
              {"Margaret", "Hamilton", "Apollo flight software"},
          }
    {
    }

    int rowCount() const override
    {
        return static_cast<int>(m_rows.size());
    }

    int columnCount() const override
    {
        return 3;
    }

    std::string data(int row, int column) const override
    {
        if (! isValidCell(row, column)) {
            return {};
        }
        return m_rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
    }

    std::string headerData(int section) const override
    {
        switch (section) {
        case 0:
            return "First name";
        case 1:
            return "Last name";
        case 2:
            return "Note";
        default:
            return {};
        }
    }

    bool isEditable(int row, int column) const override
    {
        return isValidCell(row, column);
    }

    bool setData(int row, int column, const std::string& value) override
    {
        if (! isValidCell(row, column)) {
            return false;
        }

        auto& cell = m_rows[static_cast<std::size_t>(row)][static_cast<std::size_t>(column)];
        if (cell == value) {
            return false;
        }

        cell = value;
        notifyDataChanged(row, column);
        return true;
    }

    bool insertRows(int row, int count) override
    {
        if (row < 0 || row > rowCount() || count <= 0) {
            return false;
        }

        m_rows.insert(m_rows.begin() + row,
                      static_cast<std::size_t>(count),
                      std::vector<std::string>{"New", "Person", "Editable note"});
        notifyRowsInserted(row, count);
        return true;
    }

    bool removeRows(int row, int count) override
    {
        if (row < 0 || count <= 0 || row + count > rowCount()) {
            return false;
        }

        m_rows.erase(m_rows.begin() + row, m_rows.begin() + row + count);
        notifyRowsRemoved(row, count);
        return true;
    }

private:
    bool isValidCell(int row, int column) const
    {
        return row >= 0
            && row < static_cast<int>(m_rows.size())
            && column >= 0
            && column < 3;
    }

    std::vector<std::vector<std::string>> m_rows;
};

int main()
{
    snf::Application app(0, nullptr);
    PeopleModel model;

    wg::ApplicationNode webApp;
    webApp.setTitle("SNF Table Model Example");

    auto* window = new wg::Window("TableView 3-column model", &webApp);
    window->setInitialSize(900.0f, 560.0f);
    window->setInitialPosition(32.0f, 32.0f);

    auto* mainLayout = new wg::VBoxLayout(window);
    window->setLayout(mainLayout);

    auto* title = new wg::Label("Model", window);
    mainLayout->addWidget(title);

    auto* table = new wg::TableView(window);
    table->setModel(&model);
    table->setSelectionBehavior(wg::TableSelectionBehavior::Rows);
    table->setSelectionMode(wg::TableSelectionMode::Single);
    mainLayout->addWidget(table, 3);

    auto* rowButtons = new wg::HBoxLayout(window);
    mainLayout->addWidget(rowButtons);

    auto* addRowButton = new wg::PushButton("Add row", window);
    rowButtons->addWidget(addRowButton, 1);

    auto* removeRowButton = new wg::PushButton("Remove selected row", window);
    rowButtons->addWidget(removeRowButton, 1);

    auto* form = new wg::FormLayout(window);
    mainLayout->addWidget(form);

    auto* firstNameEdit = new wg::LineEdit(window);
    auto* lastNameEdit = new wg::LineEdit(window);
    auto* noteEdit = new wg::LineEdit(window);
    form->addRow("First name", firstNameEdit);
    form->addRow("Last name", lastNameEdit);
    form->addRow("Note", noteEdit);

    auto* editButtons = new wg::HBoxLayout(window);
    editButtons->addStretch();
    mainLayout->addWidget(editButtons);

    auto* editButton = new wg::PushButton("Edit", window);
    editButtons->addWidget(editButton);

    auto* cancelButton = new wg::PushButton("Cancel", window);
    editButtons->addWidget(cancelButton);

    wg::DataWidgetMapper mapper;
    mapper.setModel(&model);
    mapper.addMapping(firstNameEdit, 0);
    mapper.addMapping(lastNameEdit, 1);
    mapper.addMapping(noteEdit, 2);

    bool editing = false;
    auto applyEditMode = [&]() {
        mapper.setMappedWidgetsEnabled(editing);
        table->setEnabled(! editing);
        addRowButton->setEnabled(! editing);
        removeRowButton->setEnabled(! editing && table->currentRow() >= 0);
        cancelButton->setEnabled(editing);
        editButton->setEnabled(editing || mapper.hasCurrentRow());
        editButton->setLabel(editing ? "Update" : "Edit");
    };

    table->currentIndexChanged.connect([&](const snf::ModelIndex& index) {
        if (! editing) {
            mapper.setCurrentIndex(index);
            applyEditMode();
        }
    });

    mapper.currentRowChanged.connect([&](int) {
        if (! editing) {
            applyEditMode();
        }
    });

    addRowButton->clicked.connect([&]() {
        if (editing) {
            return;
        }

        const int selectedRow = table->currentRow();
        const int insertAt = selectedRow >= 0 ? selectedRow + 1 : model.rowCount();
        if (model.insertRows(insertAt, 1)) {
            table->selectRow(insertAt);
            mapper.setCurrentRow(insertAt);
            applyEditMode();
        }
    });

    removeRowButton->clicked.connect([&]() {
        if (editing || table->currentRow() < 0) {
            return;
        }

        const int removedRow = table->currentRow();
        if (model.removeRows(removedRow, 1)) {
            const int nextRow = std::min(removedRow, model.rowCount() - 1);
            if (nextRow >= 0) {
                table->selectRow(nextRow);
                mapper.setCurrentRow(nextRow);
            } else {
                mapper.setCurrentRow(-1);
            }
            applyEditMode();
        }
    });

    editButton->clicked.connect([&]() {
        if (! editing) {
            if (! mapper.hasCurrentRow()) {
                return;
            }
            editing = true;
            applyEditMode();
            return;
        }

        if (mapper.submit()) {
            editing = false;
            applyEditMode();
        }
    });

    cancelButton->clicked.connect([&]() {
        if (! editing) {
            return;
        }

        mapper.revert();
        editing = false;
        applyEditMode();
    });

    table->selectRow(0);
    mapper.setCurrentRow(0);
    applyEditMode();

    webApp.initialized.connect([]() {
        std::printf("SNFWidgets table/model example ready\n");
    });

    return app.run();
}
