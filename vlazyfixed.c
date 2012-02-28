/* $Header: /home/jcb/MahJong/newmj/RCS/vlazyfixed.c,v 12.0 2009/06/28 20:43:31 jcb Rel $
 * vlazyfixed.c
 * Another slight variation on the GTK+ fixed widget.
 * It doesn't call queue_resize when children are removed, added,
 * or move, EXCEPT when the first child is added.
 * This has the consequence that move DOESN'T work, unles you
 * explicitly call resize. But that's OK, since we only use this
 * in a situation where we can physically move the child window.
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

#include "vlazyfixed.h"


static void vlazy_fixed_class_init    (GtkFixedClass    *klass);
static void vlazy_fixed_init          (VlazyFixed *widget);
static void vlazy_fixed_remove        (GtkContainer     *container,
				     GtkWidget        *widget);
static void vlazy_fixed_add (GtkContainer *container,
  GtkWidget    *widget);

static GtkFixedClass *parent_class = NULL;


GtkType
vlazy_fixed_get_type (void)
{
  static GtkType fixed_type = 0;

  if (!fixed_type)
    {
      static const GtkTypeInfo fixed_info =
      {
	"VlazyFixed",
	sizeof (VlazyFixed),
	sizeof (VlazyFixedClass),
	(GtkClassInitFunc) vlazy_fixed_class_init,
	(GtkObjectInitFunc) vlazy_fixed_init,
	/* reserved_1 */ NULL,
        /* reserved_2 */ NULL,
        (GtkClassInitFunc) NULL,
      };

      fixed_type = gtk_type_unique (GTK_TYPE_FIXED, &fixed_info);
    }

  return fixed_type;
}

static void
vlazy_fixed_class_init (GtkFixedClass *class)
{
  GtkObjectClass *object_class;
  GtkWidgetClass *widget_class;
  GtkContainerClass *container_class;

  object_class = (GtkObjectClass*) class;
  widget_class = (GtkWidgetClass*) class;
  container_class = (GtkContainerClass*) class;

  parent_class = gtk_type_class (GTK_TYPE_FIXED);

  /* override the remove, put and move method */
  container_class->remove = vlazy_fixed_remove;
  container_class->add = vlazy_fixed_add;
}


static void vlazy_fixed_init(VlazyFixed *w UNUSED) { }

GtkWidget*
vlazy_fixed_new (void)
{
  GtkFixed *fixed;

  fixed = gtk_type_new (vlazy_fixed_get_type());
  return GTK_WIDGET (fixed);
}


static void
vlazy_fixed_add (GtkContainer *container,
	       GtkWidget    *widget)
{
  g_return_if_fail (container != NULL);
  g_return_if_fail (IS_VLAZY_FIXED (container));
  g_return_if_fail (widget != NULL);

  vlazy_fixed_put (VLAZY_FIXED (container), widget, 0, 0);
}

static void
vlazy_fixed_remove (GtkContainer *container,
		  GtkWidget    *widget)
{
  GtkFixed *fixed;
  GtkFixedChild *child;
  GList *children;

  g_return_if_fail (container != NULL);
  g_return_if_fail (IS_VLAZY_FIXED (container));
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

void
vlazy_fixed_put (VlazyFixed       *vlfixed,
               GtkWidget      *widget,
               gint16         x,
               gint16         y)
{
  GtkFixed *fixed;
  GtkFixedChild *child_info;

  g_return_if_fail (vlfixed != NULL);
  g_return_if_fail (IS_VLAZY_FIXED (vlfixed));
  g_return_if_fail (widget != NULL);

  fixed = GTK_FIXED(vlfixed);
  child_info = g_new (GtkFixedChild, 1);
  child_info->widget = widget;
  child_info->x = x;
  child_info->y = y;

  gtk_widget_set_parent (widget, GTK_WIDGET (fixed));

  fixed->children = g_list_append (fixed->children, child_info); 

  if (GTK_WIDGET_REALIZED (fixed))
    gtk_widget_realize (widget);

  if (GTK_WIDGET_VISIBLE (fixed) && GTK_WIDGET_VISIBLE (widget))
    {
      if (GTK_WIDGET_MAPPED (fixed))
	gtk_widget_map (widget);
      
      if ( ! fixed->children->prev )
	gtk_widget_queue_resize (GTK_WIDGET (fixed));
    }
}

void
vlazy_fixed_move (VlazyFixed       *vlfixed,
                GtkWidget      *widget,
                gint16         x,
                gint16         y)
{
  GtkFixedChild *child;
  GtkFixed *fixed;
  GList *children;

  g_return_if_fail (vlfixed != NULL);
  g_return_if_fail (IS_VLAZY_FIXED (vlfixed));
  g_return_if_fail (widget != NULL);

  fixed = GTK_FIXED(vlfixed);
  children = fixed->children;
  while (children)
    {
      child = children->data;
      children = children->next;

      if (child->widget == widget)
        {
          child->x = x;
          child->y = y;

          break;
        }
    }
}


