/* $Header: /home/jcb/MahJong/newmj/RCS/lazyfixed.c,v 12.0 2009/06/28 20:43:12 jcb Rel $
 * lazyfixed.c
 * A slight variation on the GTK+ fixed widget.
 * It doesn't call queue_resize when children removed;
 * this can reduce flickering in widgets with many children.
 * Of course, it means that you must call gtk_widget_queue_resize manually
 * if you *do* want to remove something and have the size change.
 * Also, of course, if you remove a non-window widget, nothing will
 * happen until the next time something causes redisplay.
 */
/****************** COPYRIGHT STATEMENT **********************
 * This file is Copyright (c) 2001 by J. C. Bradfield.       *
 * This file may be used under the terms of the              *
 * GNU Lesser General Public License (any version).           *
 * The moral rights of the author are asserted.              *
 *                                                           *
 ***************** DISCLAIMER OF WARRANTY ********************
 * This code is not warranted fit for any purpose. See the   *
 * LICENCE file for further information.                     *
 *                                                           *
 *************************************************************/

#include "lazyfixed.h"


static void lazy_fixed_class_init    (GtkFixedClass    *klass);
static void lazy_fixed_init          (LazyFixed *widget);
static void lazy_fixed_remove        (GtkContainer     *container,
				     GtkWidget        *widget);

static GtkFixedClass *parent_class = NULL;


GtkType
lazy_fixed_get_type (void)
{
  static GtkType fixed_type = 0;

  if (!fixed_type)
    {
      static const GtkTypeInfo fixed_info =
      {
	"LazyFixed",
	sizeof (LazyFixed),
	sizeof (LazyFixedClass),
	(GtkClassInitFunc) lazy_fixed_class_init,
	(GtkObjectInitFunc) lazy_fixed_init,
	/* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      fixed_type = gtk_type_unique (GTK_TYPE_FIXED, &fixed_info);
    }

  return fixed_type;
}

static void
lazy_fixed_class_init (GtkFixedClass *class)
{
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;
  GtkContainerClass *container_class;

  object_class = (GtkObjectClass*) class;
  widget_class = (GtkWidgetClass*) class;
  container_class = (GtkContainerClass*) class;

  parent_class = gtk_type_class (GTK_TYPE_FIXED);

  /* override the remove method */
  container_class->remove = lazy_fixed_remove;
}


static void lazy_fixed_init(LazyFixed *w UNUSED) { }

GtkWidget*
lazy_fixed_new (void)
{
  GtkFixed *fixed;

  fixed = gtk_type_new (lazy_fixed_get_type());
  return GTK_WIDGET (fixed);
}


static void
lazy_fixed_remove (GtkContainer *container,
		  GtkWidget    *widget)
{
  GtkFixed *fixed;
  GtkFixedChild *child;
  GList *children;

  g_return_if_fail (container != NULL);
  g_return_if_fail (IS_LAZY_FIXED (container));
  g_return_if_fail (widget != NULL);

  fixed = GTK_FIXED (container);

  children = fixed->children;
  while (children)
    {
      child = children->data;

      if (child->widget == widget)
	{
	  gtk_widget_unparent (widget);

	  fixed->children = g_list_remove_link (fixed->children, children);
	  g_list_free (children);
	  g_free (child);

	  break;
	}

      children = children->next;
    }
}

