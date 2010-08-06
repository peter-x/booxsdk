
#include <QtGui/QtGui>
#include "onyx/ui/text_browser.h"
#include "onyx/sys/sys.h"

namespace ui
{

OnyxTextBrowser::OnyxTextBrowser(QWidget *parent)
: QTextBrowser(parent)
{
    setLineWrapMode(QTextEdit::WidgetWidth);
    setReadOnly(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setFrameShadow(QFrame::Plain);
    setFrameShape(QFrame::Panel);
}

OnyxTextBrowser::~OnyxTextBrowser()
{
}

void OnyxTextBrowser::mousePressEvent(QMouseEvent *me)
{
    me->accept();
    pressed_point_ = me->pos();
}

void OnyxTextBrowser::mouseReleaseEvent(QMouseEvent *me)
{
    me->accept();
    int direction = sys::SystemConfig::direction(pressed_point_, me->pos());

    if (direction == 0)
    {
        return;
    }
    if (direction > 0)
    {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_PageDown, Qt::NoModifier, "");
        QTextBrowser::keyPressEvent(&key);
    }
    else if (direction < 0)
    {
        QKeyEvent key(QEvent::KeyPress, Qt::Key_PageUp, Qt::NoModifier, "");
        QTextBrowser::keyPressEvent(&key);
    }
}

void OnyxTextBrowser::mouseDoubleClickEvent(QMouseEvent * event)
{
    event->accept();
}

void OnyxTextBrowser::resizeEvent(QResizeEvent * event)
{
    QTextDocument * doc = document();
    if (doc)
    {
        doc->setPageSize(contentsRect().size());
    }
}

void OnyxTextBrowser::keyPressEvent(QKeyEvent * ev)
{
    int key = ev->key();
    if (key == Qt::Key_PageDown ||
        key == Qt::Key_Down ||
        key == Qt::Key_Right)
    {
        key_event_.reset(new QKeyEvent(ev->type(), Qt::Key_PageDown, ev->modifiers(), ev->text()));
    }
    else if (key == Qt::Key_PageUp ||
             key == Qt::Key_Up ||
             key == Qt::Key_Left)
    {
        key_event_.reset(new QKeyEvent(ev->type(), Qt::Key_PageUp, ev->modifiers(), ev->text()));
    }
    ev->accept();
}

void OnyxTextBrowser::keyReleaseEvent(QKeyEvent * ev)
{
    ev->accept();

    if (key_event_)
    {
        QTextBrowser::keyPressEvent(key_event_.get());
    }
}

}
