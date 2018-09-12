using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class partStat  {

    public int state;


    /// <summary>
    /// Начиная с нулевого элемента, сначала те, кто в первой линии, потом во второй
    /// Если стоит флаг из девяток, то значит два типа войск в одной линии
    /// </summary>
    public List<int> typePosition;

    /// <summary>
    /// Число линий, в которые часть строится
    /// </summary>
    public int line_count;

    /// <summary>
    /// Должны быть 4 элемента
    /// </summary>
    public Vector2[] points;

    public float xsize;
    public float ysize;

    /// <summary>
    ///  Геометрический центр по четырем крайним точкам
    /// </summary>
    public Vector2 center;

    /// <summary>
    /// Контейнер со всеми группами этой части
    /// </summary>
    public List<groupStat> groups;

    /// <summary>
    ///  Список номеров полков, которые входят в эту часть
    /// </summary>
    public List<int> regiments;

    /// <summary>
    ///  Выполнить первоначальное построение
    /// </summary>
    //public void makeStartPos();

    /// <summary>
    /// Возвращает центр части по четырем крайним точкам
    /// </summary>
    //public void makeGroupCenter();

    /// <summary>
    /// По харакеристикам части расставляет полки.
    /// </summary>
    //public void makeRegimentPosition();




        public void makeGroupCenter()
    {
        Vector2 temp = new Vector2 (0, 0);
        for (int i = 0; i < points.Length; i++)
        {
            temp += points[i];
        }
        temp = temp / 4;
        center = temp; 
    }


        public int FindNumberAllRegWithThisType(int type)
        {
            int res = 0;
            for (int i = 0; i < regiments.Count; i++)
            {
                if (mainStatic.regiment_stat[(regiments[i])].Type == type || mainStatic.regiment_stat[(regiments[i])].AddType == type)
                {
                    res++;
                }
            }
            return res;
        }

        public float FindLengthAllRegWithThisType(int type)
        {
            float res = 0;
            for (int i = 0; i < regiments.Count; i++)
            {
                if (mainStatic.regiment_stat[(regiments[i])].Type == type || mainStatic.regiment_stat[(regiments[i])].AddType == type)
                {
                    res = res + ((float) mainStatic.regiment_stat[regiments[i]].regFirstLine )* mainStatic.regiment_stat[regiments[i]].regDistanseLine ;
                }
            }
            return res;
        }


        public void makeRegimentPosition()
        {
            /// Надо перевернуть для второй команды!
            int number = 0;
            float reg_length = 0;
            float temp_length = 0;
            int k = 0; /// счетчик полков этого типа
            int count_lines = 0;

            /// две переменные, для случая, когда в одной линии стоят два рода войск
            int a = 0; int b = 0;

            bool flag_double = false; // Многоразовый флаг для двух типов в одной линии

            for (int i = 0; i < typePosition.Count; i++)
            {

                if (typePosition[i] != 99999)
                {
                    if (flag_double == false)
                    {
                        number = FindNumberAllRegWithThisType(typePosition[i]);
                        reg_length = FindNumberAllRegWithThisType(typePosition[i]);

                        float distanceBetweenReg = (xsize - reg_length) / (number + 1);

                        for (int t = 0; t < regiments.Count; t++)
                        {
                            if (mainStatic.regiment_stat[(regiments[t])].Type == typePosition[i] || mainStatic.regiment_stat[(regiments[t])].AddType == typePosition[i])
                            {
                                mainStatic.regiment_stat[regiments[t]].regPosition.x = k * distanceBetweenReg + points[0].x + reg_length + mainStatic.regiment_stat[regiments[t]].regFirstLine * mainStatic.regiment_stat[regiments[t]].regDistanseLine / 2;
                                mainStatic.regiment_stat[regiments[t]].regPosition.z = points[2].y + (points[1].y - points[2].y) * count_lines / line_count;
                                reg_length += mainStatic.regiment_stat[regiments[t]].regFirstLine * mainStatic.regiment_stat[regiments[t]].regDistanseLine;
                                //reg_length += mainStatic.regiment_stat[regiments[t]].regFirstLine * mainStatic.regiment_stat[regiments[i]].regDistanseLine;
                                k++;
                            }
                        }
                        count_lines++;
                        k = 0;
                        reg_length = 0;
                    }
                    else
                    {
                        if (a == 0)
                       {
                            a = i;
                        }
                        else
                        {
                            /// Считали два, один i другой a
                            number = FindNumberAllRegWithThisType(typePosition[i]);
                            number += FindNumberAllRegWithThisType(typePosition[a]);
                            reg_length = FindNumberAllRegWithThisType(typePosition[i]);
                            reg_length += FindNumberAllRegWithThisType(typePosition[a]);
                            float distanceBetweenReg = (xsize - reg_length) / (number + 1);

                            for (int t = 0; t < regiments.Count; t++)
                            {
                                if (mainStatic.regiment_stat[(regiments[t])].Type == typePosition[i] ||
                                    mainStatic.regiment_stat[(regiments[t])].AddType == typePosition[i]||
                                    mainStatic.regiment_stat[(regiments[t])].Type == typePosition[a] ||
                                    mainStatic.regiment_stat[(regiments[t])].AddType == typePosition[a])
                                {
                                    mainStatic.regiment_stat[regiments[t]].regPosition.x = k * distanceBetweenReg + points[0].x + reg_length + mainStatic.regiment_stat[regiments[t]].regFirstLine * mainStatic.regiment_stat[regiments[t]].regDistanseLine / 2;
                                    mainStatic.regiment_stat[regiments[t]].regPosition.z = points[2].y + (points[1].y - points[2].y) * count_lines / line_count;
                                    reg_length += mainStatic.regiment_stat[regiments[t]].regFirstLine * mainStatic.regiment_stat[regiments[t]].regDistanseLine;
                                    //reg_length += mainStatic.regiment_stat[regiments[t]].regFirstLine * mainStatic.regiment_stat[regiments[i]].regDistanseLine;
                                    k++;
                                }
                            }
                            k = 0;
                            reg_length = 0;
                            count_lines++;

                        }
                    }
                }
                else
                {
                    flag_double = true;

                }

            }

        }

}


 
