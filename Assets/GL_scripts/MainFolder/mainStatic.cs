using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public struct unit_stat
{
    public bool life;
}

/// <summary>
/// Rkfcc написал андрей
/// </summary>

// ДОБАВИТЬ ПРЯМОУГОЛЬНЫЕ ЗАГОТОВКИ ПОД ПОЛКИ С ВЕКТОРОМ FORWARD с которым будем работать
static public class mainStatic
{
    static public Vector3 GetPos(regStat polk, int id)
    {
        return polk.soldiers[id].transform.position;
    }

    static public bool flag = true;
     // Контейнер, куда кладём мертвых
    static public unit200[] deads = new unit200 [600];
    static public List<ArrowControl> arr;
    static public int deathnumber;
    static public bool deathflag = false;
    static public Vector3 deathpos;
    static public int command;
    static public int deathcount = 0;

    /// <summary>
    /// Число элементов в контейнере с объектами-стрелами 
    /// </summary>
    static public int arrowCount = 0;
    static public int arrnum = 0;

    static public int x_0 = 10;
    static public float speed = 20;
    static public float[] my_pos;
    static public int test_size = 10;
    static public float TACTICPERIOD = 3;
    static public float t;
    static public float t0 = 0;

    // Данные о положении всех юнитов.
    static public Vector3[] old_unit_position;
    static public Vector3[] new_unit_position;

    /// <summary>
    /// Не нужно ?
    /// </summary>
    static public unit_stat[] unit_stat;

    /// <summary>
    /// Контейнер, содержащий параметры действующих полков
    /// </summary>
    static public List<regStat> regiment_stat;

    static public List<GeneralMod> generalMod;

    static public List<WarbandParam> warbandParam;


    /// <summary>
    /// Контейнер, содержащий всех офицеров на поле без полков
    /// </summary>
    static public List<UnitOfficer> all_officers;

    /// <summary>
    /// Контейнер, содержащий параметры бегущих полков
    /// </summary>
    static public List<regStat> panic_regiment_stat;

    /// <summary>
    /// Контейнер с номерами сражающихся построений
    /// </summary>
    static public List<figth> listFigReg;


    /// Конструктор статического класса
    static mainStatic()
    {
        my_pos = new float[test_size * 3];
        listFigReg = new List<figth>();
        regiment_stat = new List<regStat>();

    }

    static public float giveDistanse2(Vector3 a, Vector3 b)
    {
        
        return Mathf.Pow(a.x - b.x, 2) + Mathf.Pow(a.y - b.y, 2);
    }

    /// <summary>
    /// Выполняет поворот в полка в направлении точки (x z) 
    /// </summary>
    /// <param name="n"></param>
    /// <param name="x"></param>
    /// <param name="z"></param>
    /// <returns></returns>
    static public int makeRegimentRotation(int n, float x, float z)
    {
        float delta = 0.2f;
        float angle = 0;
        int res = 0;
        Vector3 t = new Vector2(0 - x + mainStatic.regiment_stat[n].regPosition.x, 0 - z + mainStatic.regiment_stat[n].regPosition.z);
        t = t.normalized;
        if (t.x > 0 && t.z > 0)
        {
            angle = Mathf.Asin(t.z) + Mathf.PI / 2;
        }
        else
        {
            if (t.x < 0 && t.z > 0)
            {
                angle = Mathf.PI / 2 + Mathf.Asin(-t.x) + Mathf.PI / 2;
            }
            else
            {
                if (t.x < 0 && t.z < 0)
                {
                    angle = Mathf.PI + Mathf.Asin(-t.z) + Mathf.PI / 2;
                }
                else
                {
                    angle = Mathf.PI * 3 / 2 + Mathf.Asin(t.x) + Mathf.PI / 2;
                }
            }
        }
        /// Угол между
        float dangle1 = 0;
        float dangle2 = 0;
        
        dangle1 = mainStatic.regiment_stat[n].angle + (2 * Mathf.PI - angle);
        dangle2 = 0 - mainStatic.regiment_stat[n].angle + angle;

        Debug.Log(dangle1);
        Debug.Log(dangle2);

        if ( Mathf.Abs(dangle1) < delta || Mathf.Abs(dangle2) < delta)
        {
            //Код ошибки тогда пора чапать на противника
            res = 1;
            return res;
        }

        if (dangle1 < dangle2)
        {
            mainStatic.regiment_stat[n].delta_angle = delta/2;
            mainStatic.regiment_stat[n].orderRotL = true;
            //makeRegimentMove(n, delta/2);
        }
        else
        {
            mainStatic.regiment_stat[n].delta_angle = delta/2;
            mainStatic.regiment_stat[n].orderRotW = true;
            //makeRegimentMove(n, delta/2);
        }
        res = 0;
        return res;
    }

    /// <summary>
    /// Функция вращения матрицы полка (номер полка, угол поворота)
    /// </summary>
    /// <param name="n"></param>
    /// <param name="a"></param>
    static public void makeRegimentRotation(int n, float a)
    {
        Debug.Log("угол");
        Debug.Log(regiment_stat[n].angle);
        for ( int num = 0; num < regiment_stat[n].soldiers.Count ; num++)
        {
            
                float x = regiment_stat[n].soldiers[num].matrix_position.x;
                float z = regiment_stat[n].soldiers[num].matrix_position.z;
                regiment_stat[n].soldiers[num].matrix_position.x = x * Mathf.Cos(a) + z * Mathf.Sin(a);
                regiment_stat[n].soldiers[num].matrix_position.z = - x * Mathf.Sin(a) + z * Mathf.Cos(a);
        }

        for (int num = 0; num < regiment_stat[n].officers.Count; num++)
        {
            float x = regiment_stat[n].officers[num].GetSoldiers().matrix_position.x;
            float z = regiment_stat[n].officers[num].GetSoldiers().matrix_position.z;
            regiment_stat[n].officers[num].GetSoldiers().matrix_position.x = x * Mathf.Cos(a) + z * Mathf.Sin(a);
            regiment_stat[n].officers[num].GetSoldiers().matrix_position.z = -x * Mathf.Sin(a) + z * Mathf.Cos(a);
        }

        regiment_stat[n].angle += a;
        if (regiment_stat[n].angle > 2 * 3.1415f)
            regiment_stat[n].angle -= (2 * 3.1415f);
        if (regiment_stat[n].angle < - 0f)
            regiment_stat[n].angle += (2 * 3.1415f);
    }

    
    /// <summary>
    /// // Функция перемещения на расстояние вперёд (номер полка, расстояние)
    /// </summary>
    /// <param name="n"> номер полка</param>
    /// <param name="move"></param>
    static public void makeRegimentMove(int n, float move)
    {
        float a = regiment_stat[n].angle;
        regiment_stat[n].regPosition.x += move * (0 * Mathf.Cos(a) + 1 * Mathf.Sin(a));
        regiment_stat[n].regPosition.z += move * (0 * Mathf.Sin(a) + 1 * Mathf.Cos(a));
    }


    static public Vector3 makeTarget( float a, float y)
    {
        Vector3 z = new Vector3(0, y, 0);
        float Pi = 3.14156f;
        float move = 1;
        z.x += move * Mathf.Cos(a);
        z.z += move * Mathf.Sin(a);
        return z;
    }

    static public Vector3 myRot(Vector3 x, float a)
    {
        Vector3 y;
        y.x = x.x * Mathf.Cos(a) + x.z * Mathf.Sin(a);
        y.y = 0f;
        y.z = 0 - x.x * Mathf.Sin(a) + x.z * Mathf.Cos(a);
        return y;
    }

    static public Vector3 myRot(Vector3 x, float a, float p)
    {
        Vector3 y; p = 0;
        y.x = x.x * Mathf.Cos(a) + x.z * Mathf.Sin(a);
        y.y = p;
        y.z = 0 - x.x * Mathf.Sin(a) + x.z * Mathf.Cos(a);
        return y;
    }
    
    /// <summary>
    /// Функция обновления координат после перемещений (номер полка) 
    /// </summary>
      static public void  updateRegimentPosition(int n)
    {
        for (int i = 0; i < regiment_stat[n].soldiers.Count; i++)
        {
            float x = regiment_stat[n].regPosition.x + regiment_stat[n].soldiers[i].matrix_position.x;
            float z = regiment_stat[n].regPosition.z + regiment_stat[n].soldiers[i].matrix_position.z;
            float y = 0f;// regiment_stat[n].regPosition.y;
            regiment_stat[n].soldiers[i].new_position = new Vector3(x, y, z);
        }

        for (int i = 0; i < regiment_stat[n].officers.Count; i++)
        {
            float x = regiment_stat[n].regPosition.x + regiment_stat[n].officers[i].GetSoldiers().matrix_position.x;
            float z = regiment_stat[n].regPosition.z + regiment_stat[n].officers[i].GetSoldiers().matrix_position.z;
            float y = 0f;
            regiment_stat[n].officers[i].GetSoldiers().new_position = new Vector3(x, y, z);
        }

        //regiment_stat[n].forw = mainStatic.makeTarget(new Vector3(0, 1, 0), mainStatic.regiment_stat[n].angle);
    }


      public static void addMeshForBattle()
      {/*
          for (int t = 0; t < mainStatic.regiment_stat.Count; t++)
              if (mainStatic.regiment_stat[t].stateFighting == true)
                  for (int i = 0; i < mainStatic.regiment_stat[t].soldiers.Count; i++)
                  {
                      if (mainStatic.regiment_stat[t].soldiers[i].gameObject.GetComponent<Rigidbody>() == null)
                      {
                          mainStatic.regiment_stat[t].soldiers[i].gameObject.AddComponent<Rigidbody>();
                          mainStatic.regiment_stat[t].soldiers[i].gameObject.GetComponent<Rigidbody>().useGravity = false;
                      }
                  }*/
      }

      public static void dellMeshForBattle()
      {
          for (int t = 0; t < mainStatic.regiment_stat.Count; t++)
              if (mainStatic.regiment_stat[t].stateFighting == false)
                  for (int i = 0; i < mainStatic.regiment_stat[t].soldiers.Count; i++)
                  {
                   //   if (mainStatic.regiment_stat[t].soldiers[i].gameObject.GetComponent<Rigidbody>() == true)
                      //    main.Destroy(mainStatic.regiment_stat[t].soldiers[i].gameObject.GetComponent<Rigidbody>());
                  }
      }

}

