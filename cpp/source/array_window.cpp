
#include <array_window.h>
#include <QBitmap>
#include <QFileDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMouseEvent>
#include <QScrollArea>
#include <QSettings>
#include <QVBoxLayout>

#include <array_window_2d.h>
#include <complex_array.h>
#include <eigenbroetler_window.h>
#include <scaled_plotter.h>

uint ArrayWindow::anon_count = 0;
static QCursor *curs = NULL;

QString const CloseSubwindowDialog::ask_on_unsaved("ask_on_unsaved");

CloseSubwindowDialog::CloseSubwindowDialog(QWidget *p, QString const& name):
    QDialog(p),
    save_requested(false)
{
    ui.setupUi(this);
    QSettings const settings(EigenbroetlerWindow::app_owner, EigenbroetlerWindow::app_name);
    ui.dontaskCheckBox->setChecked(!settings.value("ask_on_unsaved", true).toBool());
    ui.instructionLabel->setText(QString(tr("Window <b>%1</b> has not been saved. Save now?")).arg(name));
}

CloseSubwindowDialog::~CloseSubwindowDialog()
{
    QSettings settings(EigenbroetlerWindow::app_owner, EigenbroetlerWindow::app_name);
    ui.dontaskCheckBox->isChecked();
    settings.setValue("ask_on_unsaved", !ui.dontaskCheckBox->isChecked());
}

static void cleanup()
{
    delete curs;
}

ArrayWindow *ArrayWindow::createWindow(ComplexArray *data,
                                       DisplayInfo::ComplexComponent c,
                                       DisplayInfo::Scale s,
                                       DisplayInfo::ColourMap const& p)
{
    ArrayWindow *w = new ArrayWindow2D(data, c, s, p);
    if (w->getData()->isValid()) {
        return w;
    }
    else {
        delete w;
        return NULL;
    }
}

ArrayWindow::ArrayWindow(ComplexArray *cdata,
                         DisplayInfo::ComplexComponent c,
                         DisplayInfo::Scale s):
    QWidget(),
    d(cdata),
    cmp(c),
    scl(s)
{
    setContentsMargins (0, 0, 0, 0);
    QVBoxLayout *verticalLayout = new QVBoxLayout(this);
    verticalLayout->setContentsMargins (0, 0, 0, 0);

    QHBoxLayout *horizontalLayout = new QHBoxLayout();
    horizontalLayout->setContentsMargins (0, 0, 0, 0);
    verticalLayout->addLayout(horizontalLayout);

    QVBoxLayout *pholder = new QVBoxLayout;
    pholder->setAlignment(Qt::AlignTop | Qt::AlignRight);
    horizontalLayout->addLayout(pholder);
    colour_map = new Plotter(40, 256);
    pholder->addWidget(colour_map);

    plotLayout = new QWidget;
    left_plot = new ScaledPlotter(d->width(), d->height(), this);
    left_plot->setParent(plotLayout);
    left_plot->move(0, 0);
    left_plot->setCursor(Qt::CrossCursor);
    right_plot = new ScaledPlotter(d->width(), d->height(), this);
    right_plot->setParent(plotLayout);
    right_plot->move(left_plot->width(), 0);
    right_plot->setCursor(Qt::CrossCursor);

    plotLayout->setFixedSize(left_plot->width() + right_plot->width(), left_plot->height());
    QScrollArea *scrollArea = new QScrollArea;
    scrollArea->setWidget(plotLayout);
    horizontalLayout->addWidget(scrollArea);
    scrollArea->setFrameStyle(QFrame::NoFrame);

    QPalette status_palette;
    QBrush brush(QColor(192, 255, 192, 255));
    brush.setStyle(Qt::SolidPattern);
    status_palette.setBrush(QPalette::Active, QPalette::Base, brush);
    status = new QLineEdit*[2];
    status[0] = new QLineEdit;
    status[1] = new QLineEdit;
    // Reduce font size for satus info. Might be too small?
    QFont status_font = status[0]->font();
    status_font.setPointSize(status_font.pointSize() - 2);
    QHBoxLayout *statusLayout = new QHBoxLayout;
    verticalLayout->addLayout(statusLayout);
    for (int i = 0; i < 2; ++i) {
        statusLayout->addWidget(status[i]);
        status[i]->setText("");
        status[i]->setReadOnly(true);
        status[i]->setPalette(status_palette);
        status[i]->setFont(status_font);
        //status[i]->setPreferredWidth(256);
        //status[i]->setFrame(false);
    }
    //statusLayout->addStretch();
    updateTitle();

    if (curs == NULL) {
        curs = new QCursor(QBitmap(":/resources/cross_map.pbm"),
                           QBitmap(":/resources/cross_mask.pbm"));
        atexit(cleanup);
    }
        left_plot->setCursor(*curs);
        right_plot->setCursor(*curs);
}

void ArrayWindow::updateTitle()
{
    QString title = d->source();
    if (!title.isEmpty()) {
        int slash = title.lastIndexOf('/');
        if (slash >= 0)
            title = title.mid(slash + 1);
    }
    else {
        setWindowTitle(title.sprintf("eigenbrot_%04u", ++anon_count));
    }
    setWindowTitle(title);
}

void ArrayWindow::closeEvent(QCloseEvent *evt)
{
    if (!d->source().isEmpty())
        evt->accept();
    else {
        QSettings const settings(EigenbroetlerWindow::app_owner, EigenbroetlerWindow::app_name);
        bool ask = settings.value(CloseSubwindowDialog::ask_on_unsaved, true).toBool();
        if (ask) {
            CloseSubwindowDialog msgBox(this, windowTitle());
            if (msgBox.exec() != QDialog::Rejected) {
                if (msgBox.saveRequested()) {
                    if (saveData())
                        evt->accept();
                    else
                        evt->ignore();
                }
            }
            else
                evt->ignore();
        }
        else
            evt->accept();
    }
}

ArrayWindow::~ArrayWindow()
{
    delete [] status;
    delete d;
}

void ArrayWindow::setColourMap(DisplayInfo::ColourMap const& p)
{
    if (pal != p) {
        pal = p;
        colour_map->setBackground(192, 192, 192);
        colour_map->clear();
        colour_map->setForeground(0, 0, 0);
        for (int i = 0; i < DisplayInfo::COLOURMAP_SIZE; i += 64)
            colour_map->drawLine(0, i, colour_map->width(), i);
        for (int i = 0; i < DisplayInfo::COLOURMAP_SIZE; ++i) {
            colour_map->setForeground(pal[DisplayInfo::COLOURMAP_SIZE - i - 1]);
            colour_map->drawLine(10, i, colour_map->width() - 10, i);
        }
        colour_map->repaint();
        redraw();
    }
}

bool ArrayWindow::saveData()
{
    if (d->isValid()) {
        QString fileTypes(tr("FITS Files (*.fits *.fit);;All files (*.*)"));
        QSettings settings(EigenbroetlerWindow::app_owner, EigenbroetlerWindow::app_name);
        QString dir = QFile::decodeName(settings.value(EigenbroetlerWindow::last_save, QString()).toString().toAscii());
        QString fileName = QFileDialog::getSaveFileName(this, tr("Save file"),
                                                        dir, fileTypes, 0, QFileDialog::DontUseNativeDialog);
        if (!fileName.isEmpty()) {
            fileName = fileName.toUtf8();
            int slash = fileName.lastIndexOf('/');
            settings.setValue(EigenbroetlerWindow::last_save, fileName.left(slash + 1));
            if (!d->save(fileName))
                QMessageBox::warning(this, "File save failed", d->errorString());
            else {
                updateTitle();
                return true;
            }
        }
    }
    return false;
}
